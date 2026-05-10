#pragma once

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <string>
#include <stdexcept>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <utility>

namespace network {

class TCPSocket {
public:
    TCPSocket(const std::string& port_number) {
        struct addrinfo hints;
        struct addrinfo *socket_info = nullptr;

        std::memset(&hints, 0, sizeof(struct addrinfo));
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_flags = AI_PASSIVE;

        if (getaddrinfo(NULL, port_number.c_str(), &hints, &socket_info) != 0) {
            throw std::runtime_error("TCPSocket(): Unable to obtain address info for own server's port number: " + port_number);
        }

        socket_fd = socket(socket_info->ai_family, socket_info->ai_socktype, socket_info->ai_protocol);
        if (socket_fd == -1) {
            freeaddrinfo(socket_info);
            throw std::runtime_error("TCPSocket(): Unable to create a socket file descriptor for own server's port number: " + port_number);
        }

        int option = 1;
        if (setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &option, sizeof(option)) == -1) {
            close(socket_fd);
            freeaddrinfo(socket_info);
            throw std::runtime_error("TCPSocket(): Unable to set SO_REUSEADDR for own server's port number: " + port_number);
        }
        
        if (int rc = bind(socket_fd, socket_info->ai_addr, socket_info->ai_addrlen); rc == -1) {
            close(socket_fd);
            freeaddrinfo(socket_info);
            throw std::runtime_error("TCPSocket(): Unable to bind socket to port number: " + port_number);
        }

        freeaddrinfo(socket_info);
    }

    ~TCPSocket() {
        if (socket_fd != -1) {
            close(socket_fd);
        }
    }

    // disallow copying
    TCPSocket(const TCPSocket&) = delete;
    TCPSocket& operator=(const TCPSocket&) = delete;

    // allow moving of socket objects
    TCPSocket(TCPSocket&& other) noexcept
        : socket_fd { std::exchange(other.socket_fd, -1) }
    { }

    TCPSocket& operator=(TCPSocket&& other) noexcept {
        if (this == &other) {
            return *this;
        }

        if (socket_fd != -1) {
            close(socket_fd);
        }

        socket_fd = std::exchange(other.socket_fd, -1);
        return *this;
    }

    void listen(int backlog = 128) {
        int rc = ::listen(socket_fd, backlog);
        if (rc == -1) {
            throw std::runtime_error("TCPSocket::listen(): Unable to listen on socket.");
        }
    }

    int accept() {
        struct sockaddr_storage client_info;
        socklen_t client_info_len = sizeof(struct sockaddr_storage);
        int rc = ::accept(socket_fd, (struct sockaddr *)&client_info, &client_info_len);

        if (rc == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return -1;
            }
            throw std::runtime_error("TCPSocket::accept(): Unable to accept a connection on socket.");
        }
        return rc;
    }

    /*
    * Attempts to connect to server and port number.
    * Returns the return value of calling connect().
    * Likely to return -1 since we usually use non blocking sockets (EINPROGRESS)
    */
    int connect(const std::string& server, const std::string& port_number) {
        struct addrinfo hints, *res;
        memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;

        if (getaddrinfo(server.c_str(), port_number.c_str(), &hints, &res) != 0) [[unlikely]] {
            throw std::runtime_error(
                "TCPSocket::connect(): Unable to get address information for server: " 
                + server + " and port number: " + port_number + ".");
        }

        return ::connect(socket_fd, res->ai_addr, res->ai_addrlen);
    }

    void set_non_blocking() {
        int flags = fcntl(socket_fd, F_GETFL, 0);
        if (flags == -1) [[unlikely]] {
            throw std::runtime_error("TCPSocket::set_non_blocking(): Unable to get socket's flags");
        }

        int rc = fcntl(socket_fd, F_SETFL, flags | O_NONBLOCK);
        if (rc == -1) [[unlikely]] {
            throw std::runtime_error("TCPSocket::set_non_blocking(): Unable to set socket to be non blocking.");
        }
    }

    /*
    * Pass in 0 to disable, 1 to enable Nagle's Algorithm
    */
    void set_no_delay(int enable) {
        int rc = setsockopt(socket_fd, IPPROTO_TCP, TCP_NODELAY, &enable, sizeof(enable));
        if (rc < 0) [[unlikely]] {
            throw std::runtime_error(
                "TCPSocket::set_no_delay(): Unable to set/unset socket's Nagle's Algorithm. Error: " 
                + std::string(strerror(errno)));
        }
    }

    /*
    * Pass in 0 to disable, 1 to enable QUICKACK
    */
    void set_quick_ack(int enable) {
        int rc = setsockopt(socket_fd, IPPROTO_TCP, TCP_QUICKACK, &enable, sizeof(enable));
        if (rc < 0) [[unlikely]] {
            throw std::runtime_error(
                "TCPSocket::set_quick_ack(): Unable to set/unset socket's QUICKACK. Error: " 
                + std::string(strerror(errno)));
        }
    }

    int get_fd() { return socket_fd; }

private:
    int socket_fd = -1;
};

} // namespace network