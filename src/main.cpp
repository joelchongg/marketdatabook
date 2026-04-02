#include "network/EpollReactor.h"
#include "network/TCPSocket.h"
#include "protocol/ItchParser.h"

#define SERVER_PORT_NUMBER "58362"

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

    protocol::ItchParser parser{};
    network::EpollReactor<protocol::ItchParser> epoller { parser };

    epoller.add_socket(server.get_fd());
    epoller.run(server);
    

    return 0;
}