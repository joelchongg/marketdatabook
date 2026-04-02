#include "book/LimitOrderBook.h"
#include "network/EpollReactor.h"
#include "network/TCPSocket.h"
#include "protocol/ItchParser.h"
#include "utils/spscqueue.h"

#include <thread>

#define SERVER_PORT_NUMBER "58362"

template <size_t QUEUE_SIZE>
void consume(utils::SPSCQueue<protocol::AddOrder, QUEUE_SIZE>& queue, book::LimitOrderBook& book) {
    while (true) {
        protocol::AddOrder order;
        while (!queue.pop(order));
        // debugging
        std::cout << "Consuming order...\n";


        std::cout << "New Order Reference Number: " << order.order_reference_number << '\n';
        std::cout << "New Order Shares: " << order.shares << '\n';
        std::cout << "New Order Price: " << order.price << '\n';

        book.add_order(order);
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
    // utils::SPSCQueue<protocol::AddOrder, 1048576> queue;
    utils::SPSCQueue<protocol::AddOrder, 4096> queue;
    book::LimitOrderBook book(1000);


    protocol::ItchParser parser{queue};
    network::EpollReactor epoller { parser };

    epoller.add_socket(server.get_fd());

    // launch consumer thread
    std::thread consumer{consume<4096>, std::ref(queue), std::ref(book)};

    epoller.run(server);
    

    return 0;
}