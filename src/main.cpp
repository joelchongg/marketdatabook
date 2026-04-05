#include "book/LimitOrderBook.h"
#include "network/EpollReactor.h"
#include "network/TCPSocket.h"
#include "protocol/ItchParser.h"
#include "protocol/OrderTypes.h"
#include "utils/spscqueue.h"

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
    // should pin thread before it executes so that it won't keep changing between cores
    
    while (true) {
        T order;
        while (!queue.pop(order));
        // debugging
        std::cout << "Consuming order...\n";

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
    // char buffer[36] { 
    //     0x41, 
    //     0x00, 0x00, 
    //     0x00, 0x01,
    //     0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
    //     0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
    //     0x53,
    //     0x00, 0x00, 0x00, 0x64,
    //     0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
    //     0x00, 0x03, 0x0D, 0x40
    // };

    network::TCPSocket server(SERVER_PORT_NUMBER);
    server.set_non_blocking();
    server.listen();

    // used objects
    utils::SPSCQueue<Orders, 4096> queue;
    book::LimitOrderBook book(1000);


    protocol::ItchParser parser{queue};
    network::EpollReactor epoller { parser };

    epoller.add_socket(server.get_fd());

    // launch consumer thread
    std::thread consumer{consume<Orders, 4096>, std::ref(queue), std::ref(book)};

    epoller.run(server);
    

    return 0;
}