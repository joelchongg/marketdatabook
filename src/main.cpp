#include "book/LimitOrderBook.h"
#include "network/EpollReactor.h"
#include "network/MulticastReceiver.h"
#include "network/TCPSocket.h"
#include "protocol/ItchParser.h"
#include "protocol/ItchOrderTypes.h"
#include "utils/spscqueue.h"
#include "utils/MmapLogger.h"

#include <immintrin.h>
#include <sys/eventfd.h>
#include <pthread.h>
#include <thread>
#include <variant>

#define HOST_INTERFACE "eth0"
#define MULTICAST_IP_ADDR "224.0.0.0"
#define MOCK_MODE true

using Orders = std::variant<protocol::NormalizedAddOrder, protocol::NormalizedOrderExecuted, protocol::NormalizedCancelOrder>;

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

template <typename T, size_t QUEUE_SIZE>
void consume(utils::SPSCQueue<T, QUEUE_SIZE>& queue, book::LimitOrderBook& book) {
    // pin to core 2 (beside epoll thread to minimize inter-core latency)
    cpu_set_t cpu_set;
    CPU_ZERO(&cpu_set);
    CPU_SET(2, &cpu_set);

    // make sure with perf that there is no thread migration (or at most 1 when setaffinity is called)
    pthread_t current_thread = pthread_self();
    int ret = pthread_setaffinity_np(current_thread, sizeof(cpu_set_t), &cpu_set);
    if (ret != 0) [[unlikely]] {
        throw std::runtime_error("LOB Thread: Unable to pin LOB thread to Core 2. Error Code: " + std::to_string(ret));
    }
    
    while (true) {
        T order;
        while (!queue.pop(order)) { _mm_pause(); };
        // debugging
        // std::cout << "Consuming order...\n";

        std::visit([&](auto& order) {
            if constexpr (std::is_same_v<std::decay_t<decltype(order)>, protocol::NormalizedAddOrder>) {
                book.add_order(order);
            } else if constexpr (std::is_same_v<std::decay_t<decltype(order)>, protocol::NormalizedOrderExecuted>) {
                book.execute_order(order);
            } else if constexpr (std::is_same_v<std::decay_t<decltype(order)>, protocol::NormalizedCancelOrder>) {
                book.cancel_order(order.order_reference_number);
            }
        }, order);
    }
}

void run_mock_mode() {
    utils::MmapLogger logger("logs/event_logs.txt");
    utils::SPSCQueue<Orders, 4096> queue;
    book::LimitOrderBook book(1000);

    protocol::ItchParser parser {queue, logger};
    network::MulticastReceiver poller(parser, true);

    // launch consumer thread (pinned to core 2)
    std::thread lob_thread{consume<Orders, 4096>, std::ref(queue), std::ref(book)};

    // launch statistics thread (pinned to core 7)
    std::thread statistics_thread{&network::MulticastReceiver<decltype(parser)>::print_statistics_mock_mode, std::ref(poller)};

    // launch thread to simulate kernel packets (pinned to core 0)
    const std::string PCAP_FILENAME = "pcap_file.pcap";
    std::thread kernel_packet_thread{&network::MulticastReceiver<decltype(parser)>::simulate_packets, std::ref(poller), PCAP_FILENAME};

    // main thread runs polling
    poller.poll();
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
    std::thread lob_thread{consume<Orders, 4096>, std::ref(queue), std::ref(book)};

    // debug: launch separate thread for printing statistics
    std::thread packet_stats_thread{&network::MulticastReceiver<decltype(parser)>::print_statistics, std::ref(poller)};

    // main thread runs multicast polling
    poller.poll();

    return 0;
}