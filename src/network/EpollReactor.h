#pragma once

#include <array>
#include <concepts>
#include <fcntl.h>
#include <sys/epoll.h>
#include <stdexcept>
#include <unistd.h>
#include <utility>
#include <vector>

#include "TCPSocket.h"

namespace network {

template <typename T>
concept HasOnDataMethod = requires(T obj, int fd, void* buf, int bytes) { 
    { obj.on_data(fd, buf, bytes) } -> std::same_as<int>;
};

template <HasOnDataMethod MessageHandler>
class EpollReactor {
private:
    // ConnectionState struct is used to have a buffer for each file descriptor in the interest list
    // This ensures that we do not keep creating local temporary buffers
    struct ConnectionState {
        std::array<char, 8192> data{};
        size_t offset{};
    };

public:
    EpollReactor(MessageHandler& handler) 
        : handler_ { handler }, epoll_fd_ { epoll_create1(EPOLL_CLOEXEC) } {
        if (epoll_fd_ == -1) {
            throw std::runtime_error("Unable to create epoll instance.");
        }
    }

    ~EpollReactor() {
        if (epoll_fd_ != -1) {
            close(epoll_fd_);
        }
    }

    // disable copy semantics
    EpollReactor(const EpollReactor&) = delete;
    EpollReactor& operator=(const EpollReactor&) = delete;

    // allow move semantics
    EpollReactor(EpollReactor&& other)
        : epoll_fd_ { std::exchange(other.epoll_fd_, -1) }
    { }

    EpollReactor& operator=(EpollReactor&& other) {
        if (this == &other) {
            return *this;
        }

        if (epoll_fd_ != -1) {
            close(epoll_fd_);
        }

        epoll_fd_ = std::exchange(other.epoll_fd_, -1);
        return *this;
    }

    void add_socket(int fd) {
        if (fd == -1) [[unlikely]] { 
            throw std::runtime_error("File descriptor passed in is invalid.");
        }

        struct epoll_event event{};
        // edge triggered for receiving inputs
        event.events = EPOLLIN | EPOLLET;
        event.data.fd = fd;

        int rc = epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &event);
        if (rc == -1) {
            throw std::runtime_error("Unable to add file descriptor");
        }
    }

    void run(TCPSocket& server_socket) {
        while (true) {

            struct epoll_event events[MAX_EVENTS];
            int ready_count = epoll_wait(epoll_fd_, events, MAX_EVENTS, -1); // -1 to block until an event occurs

            if (ready_count == -1) {
                if (errno == EINTR) {
                    continue;
                }
                throw std::runtime_error("Unexpected error occurred during epoll_wait.");
            }
    
            for (int i = 0; i < ready_count; ++i) {
                int fd = events[i].data.fd;
                // a new client is trying to connect, accept them if possible
                if (server_socket.get_fd() == fd) {
                    while (true) {
                        int client_fd = server_socket.accept();
                        if (client_fd == -1) {
                            break;
                        }
    
                        int client_fd_flags = fcntl(client_fd, F_GETFL, 0);
                        if (client_fd_flags == -1 || fcntl(client_fd, F_SETFL, client_fd_flags | O_NONBLOCK) == -1) {
                            close(client_fd);
                            continue;
                        };
                        add_socket(client_fd);
                    }
                } else {
                    // a client socket woke up instead
                    // Note: make sure to read all of the data as we are using edge triggered. See man page for details
                    while (true) {
                        ConnectionState& buffer = fd_buffers[fd];
                        int bytes_received = recv(fd, &buffer.data[buffer.offset], 8192 - buffer.offset, 0);
                        if (bytes_received == -1) {
                            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                                // no more data left in the buffer
                                break;
                            }
                            // unexpected error, close the client file descriptor
                            buffer.offset = 0;
                            close(fd);
                            break;
                        } else if (bytes_received == 0) {
                            // client disconnected
                            buffer.offset = 0; // reset buffer for next file descriptor use
                            close(fd);
                            break;
                        } else {
                            // process data
                            buffer.offset += bytes_received;
                            int bytes_consumed = handler_.on_data(fd, buffer.data.data(), buffer.offset);
                            std::memmove(buffer.data.data(), &buffer.data[bytes_consumed], buffer.offset - bytes_consumed);
                            buffer.offset = buffer.offset - bytes_consumed;
                        }
                    }
                }
            }
        }
    }

    int get_epollfd() { return epoll_fd_; }

private:
    std::vector<ConnectionState> fd_buffers = std::vector<ConnectionState>(MAX_CONNECTIONS);
    MessageHandler& handler_;
    int epoll_fd_ { -1 };

    static const int MAX_EVENTS { 64 };
    static const int MAX_BUFFER_SIZE { 4096 }; // should be tuned accordingly (align with page size)
    static const int MAX_CONNECTIONS { 1000 }; // should be tuned when we have more information

    using TCPSocket = network::TCPSocket;
};

}