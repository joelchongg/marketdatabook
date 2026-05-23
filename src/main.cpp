#include "book/LimitOrderBook.h"
#include "network/EpollReactor.h"
#include "network/MulticastReceiver.h"
#include "network/TCPSocket.h"
#include "protocol/ItchParser.h"
#include "protocol/ItchOrderTypes.h"
#include "telemetry/TelemetryEngine.h"
#include "telemetry/TSC_Clock.h"
#include "utils/spscqueue.h"
#include "utils/MmapLogger.h"

#include <fstream>
#include <iostream>
#include <immintrin.h>
#include <sys/eventfd.h>
#include <pthread.h>
#include <thread>
#include <variant>

#define HOST_INTERFACE "eth0"
#define MULTICAST_IP_ADDR "224.0.0.0"
#define MOCK_MODE true

using Orders = protocol::NormalizedOrder;

struct OrderBookCallBack {
    void operator()(book::LimitOrderBook& book, protocol::NormalizedAddOrder& new_order) {
        book.add_order(new_order);
    }

    void operator()(book::LimitOrderBook& book, protocol::NormalizedOrderExecuted& new_order) {
        book.execute_order(new_order);
    }

    void operator()(book::LimitOrderBook& book, protocol::NormalizedCancelOrder& new_order) {
        book.cancel_order(new_order.order_reference_number);
    }
};

template <typename T, size_t QUEUE_SIZE, size_t TELEMETRY_BUFFER_SIZE, size_t NUM_BUFFERS>
void log_telemetry(utils::SPSCQueue<T, QUEUE_SIZE>& tel_queue, 
                   std::array<std::array<std::tuple<uint64_t, uint64_t, uint64_t>, TELEMETRY_BUFFER_SIZE>, NUM_BUFFERS>& bufs) {
    // pin to core 6 (away from all other threads)
    cpu_set_t cpu_set;
    CPU_ZERO(&cpu_set);
    CPU_SET(6, &cpu_set);

    // make sure with perf that there is no thread migration (or at most 1 when setaffinity is called)
    pthread_t current_thread = pthread_self();
    int ret = pthread_setaffinity_np(current_thread, sizeof(cpu_set_t), &cpu_set);
    if (ret != 0) [[unlikely]] {
        throw std::runtime_error("Telemetry Logging Thread: Unable to pin thread to Core 6. Error Code: " + std::to_string(ret));
    }


    std::ofstream output_file("./logs/telemetry_log.txt", std::ios::binary | std::ios::out);
    if (!output_file) [[unlikely]] {
        std::cerr << "Failed to open telemetry file!" << std::endl;
        return;
    }

    while (true) {
        size_t buf_idx;
        while (!tel_queue.pop(buf_idx)) { _mm_pause(); }        

        output_file.write(reinterpret_cast<const char*>(bufs[buf_idx].data()),
                    sizeof(std::tuple<uint64_t, uint64_t, uint64_t>) * TELEMETRY_BUFFER_SIZE);
    }

    output_file.close();
}

template <size_t QUEUE_SIZE>
void consume(utils::SPSCQueue<protocol::NormalizedOrder, QUEUE_SIZE>& queue, book::LimitOrderBook& book) {
    // pin to core 2 (beside epoll thread to minimize inter-core latency)
    cpu_set_t cpu_set;
    CPU_ZERO(&cpu_set);
    CPU_SET(2, &cpu_set);

    // make sure with perf that there is no thread migration (or at most 1 when setaffinity is called)
    pthread_t current_thread = pthread_self();
    int ret = pthread_setaffinity_np(current_thread, sizeof(cpu_set_t), &cpu_set);
    if (ret != 0) [[unlikely]] {
        throw std::runtime_error("LOB Thread::consume(): Unable to pin LOB thread to Core 2. Error Code: " + std::to_string(ret));
    }

    // create telemetry objects used for profiling
    uint64_t l1d_miss_config = (PERF_COUNT_HW_CACHE_L1D) |
                               (PERF_COUNT_HW_CACHE_OP_READ << 8) |
                               (PERF_COUNT_HW_CACHE_RESULT_MISS << 16);
    
    telemetry::TelemetryEngine l1d_te(PERF_TYPE_HW_CACHE, l1d_miss_config);
    telemetry::TelemetryEngine branch_te(PERF_TYPE_HARDWARE, PERF_COUNT_HW_BRANCH_MISSES);
    telemetry::TSC_Clock clock;

    constexpr size_t NUM_BUFFERS = 2;
    constexpr size_t TELEMETRY_BUFFER_SIZE = 16384; // arbitrary value (power of 2 to allow bitwise for modulo)
    std::array<std::array<std::tuple<uint64_t, uint64_t, uint64_t>, TELEMETRY_BUFFER_SIZE>, NUM_BUFFERS> buffers;
    utils::SPSCQueue<size_t, 2> telemetry_handoff_queue;    // used to pass buffer to another thread to perform the logging of profiling data
    size_t active_buf { 0 };
    size_t tel_idx { 0 };

    // create telemetry thread used to log results
    std::thread telemetry_thread{[&telemetry_handoff_queue, &buffers]() {
        log_telemetry(telemetry_handoff_queue, buffers);
    }};

    // dispatch table to avoid std::variant branching
    static constexpr void (*dispatch_table[4])(book::LimitOrderBook&, const protocol::NormalizedOrder&) = {
        [](book::LimitOrderBook& b, const protocol::NormalizedOrder& o) { b.add_order(o.add_order); },
        [](book::LimitOrderBook& b, const protocol::NormalizedOrder& o) { b.execute_order(o.execute_order); },
        [](book::LimitOrderBook& b, const protocol::NormalizedOrder& o) { b.cancel_order(o.cancel_order.order_reference_number); }
    };

    while (true) {
        protocol::NormalizedOrder order;
        while (!queue.pop(order)) { _mm_pause(); };
        // debugging
        // std::cout << "Consuming order...\n";

        // measurements start
        uint64_t start_l1d = l1d_te.read_pmc();
        uint64_t start_branches = branch_te.read_pmc();
        uint64_t start_tsc = clock.rdtsc_start();

        // index the dispatch table based on the order type enum
        dispatch_table[static_cast<uint8_t>(order.type) & 0xFF](book, order);

        // measurements end
        uint64_t end_tsc = clock.rdtsc_end();
        uint64_t end_branches = branch_te.read_pmc();
        uint64_t end_l1d = l1d_te.read_pmc();

        // record measurements if needed
        uint64_t tsc_delta = end_tsc - start_tsc;
        uint64_t l1d_delta = end_l1d - start_l1d;
        uint64_t branch_delta = end_branches - start_branches;


        buffers[active_buf][tel_idx] = {tsc_delta, l1d_delta, branch_delta};
        tel_idx = (tel_idx + 1) & (TELEMETRY_BUFFER_SIZE - 1);

        // tuned based on requirements
        if (l1d_delta > 1000 || tsc_delta > 5000) [[unlikely]] {
            telemetry_handoff_queue.push(active_buf);
            active_buf = 1 - active_buf;
            tel_idx = 0;
        }
    }
}

void run_mock_mode() {
    utils::MmapLogger logger("logs/event_logs.txt");
    utils::SPSCQueue<Orders, 4096> queue;
    book::LimitOrderBook book(1000);

    protocol::ItchParser parser {queue, logger};
    network::MulticastReceiver poller(parser, true);

    // launch consumer thread (pinned to core 2)
    std::thread lob_thread{consume<4096>, std::ref(queue), std::ref(book)};

    // launch statistics thread (pinned to core 7)
    std::thread statistics_thread{&network::MulticastReceiver<decltype(parser)>::print_statistics_mock_mode, std::ref(poller)};

    // launch thread to simulate kernel packets (pinned to core 0)
    const std::string PCAP_FILENAME = "./files/01302019.NASDAQ_ITCH50";
    std::thread kernel_packet_thread{&network::MulticastReceiver<decltype(parser)>::simulate_packets, std::ref(poller), PCAP_FILENAME};

    // main thread runs polling simulation
    poller.poll_simulation();
}

int main() {
    // pin current thread which will be the epoll thread to one core (pin to core 1)
    cpu_set_t cpu_set;
    CPU_ZERO(&cpu_set);
    CPU_SET(1, &cpu_set);

    // make sure with perf that there is no thread migration (or at most 1 when setaffinity is called)
    pthread_t current_thread = pthread_self();
    int ret = pthread_setaffinity_np(current_thread, sizeof(cpu_set_t), &cpu_set);

    if (ret != 0) [[unlikely]] {
        throw std::runtime_error("Unable to pin main thread to Core 1. Error Code: " + std::to_string(ret));
    }

    if (MOCK_MODE) {
        run_mock_mode();
        return 0;
    }

    // create logging file
    utils::MmapLogger logger("logs/event_logs.txt");

    utils::SPSCQueue<Orders, 4096> queue;
    book::LimitOrderBook book(1000);

    protocol::ItchParser parser {queue, logger};
    network::MulticastReceiver poller {parser, HOST_INTERFACE, MULTICAST_IP_ADDR};

    // launch consumer thread
    std::thread lob_thread{consume<4096>, std::ref(queue), std::ref(book)};

    // debug: launch separate thread for printing statistics
    std::thread packet_stats_thread{&network::MulticastReceiver<decltype(parser)>::print_statistics, std::ref(poller)};

    // main thread runs multicast polling
    poller.poll();

    return 0;
}