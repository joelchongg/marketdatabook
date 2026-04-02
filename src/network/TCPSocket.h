#pragma once

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <netdb.h>
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
            throw std::runtime_error("Unable to obtain address info");
        }

        socket_fd = socket(socket_info->ai_family, socket_info->ai_socktype, socket_info->ai_protocol);
        if (socket_fd == -1) {
            freeaddrinfo(socket_info);
            throw std::runtime_error("Unable to obtain socket");
        }

        int option = 1;
        if (setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &option, sizeof(option)) == -1) {
            close(socket_fd);
            freeaddrinfo(socket_info);
            throw std::runtime_error("Unable to set SO_REUSEADDR");
        }
        
        if (int rc = bind(socket_fd, socket_info->ai_addr, socket_info->ai_addrlen); rc == -1) {
            close(socket_fd);
            freeaddrinfo(socket_info);
            throw std::runtime_error("Unable to bind socket to port number: " + port_number);
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
            throw std::runtime_error("Unable to listen on this socket.");
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
            throw std::runtime_error("Unable to accept a connection on this socket.");
        }
        return rc;
    }

    void set_non_blocking() {
        int flags = fcntl(socket_fd, F_GETFL, 0);
        if (flags == -1) {
            throw std::runtime_error("Unable to get socket's flags");
        }

        int rc = fcntl(socket_fd, F_SETFL, flags | O_NONBLOCK);
        if (rc == -1) {
            throw std::runtime_error("Unable to set socket to be non blocking.");
        }
    }

    int get_fd() { return socket_fd; }

private:
    int socket_fd = -1;
};

} // namespace network