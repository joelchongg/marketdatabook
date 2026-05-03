#include "book/LimitOrderBook.h"
#include "network/EpollReactor.h"
#include "network/TCPSocket.h"
#include "protocol/ItchParser.h"
#include "protocol/OrderTypes.h"
#include "utils/spscqueue.h"
#include "utils/MmapLogger.h"

#include <sys/eventfd.h>
#include <pthread.h>
#include <thread>
#include <variant>

#define SERVER_PORT_NUMBER "58362"

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
        while (!queue.pop(order));
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

    // create logging file
    utils::MmapLogger logger("logs/event_logs.txt");

    network::TCPSocket server(SERVER_PORT_NUMBER);
    server.set_non_blocking();
    server.listen();

    // used objects
    utils::SPSCQueue<Orders, 4096> queue;
    book::LimitOrderBook book(1000);

    utils::SPSCQueue<int, 1024> incoming_connections_queue;
    int event_fd = eventfd(0, EFD_CLOEXEC);
    if (event_fd == -1) [[unlikely]] {
        throw std::runtime_error("Main: Unable to create event fd. Error: " + std::string(strerror(errno)));
    }

    protocol::ItchParser parser {queue};
    network::EpollReactor epoller {parser, incoming_connections_queue, event_fd, true};
    network::EpollReactor connections {parser, incoming_connections_queue, event_fd, false};

    epoller.add_socket(server.get_fd());

    // launch thread for checking new connections
    std::thread incoming_connections_thread{&network::EpollReactor<decltype(parser)>::wait_for_connections, &connections, std::ref(server)};

    // launch consumer thread
    std::thread lob_thread{consume<Orders, 4096>, std::ref(queue), std::ref(book)};

    // main thread runs epoll
    epoller.run();
        

    return 0;
}