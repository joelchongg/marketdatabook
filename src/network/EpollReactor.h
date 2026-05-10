#pragma once

#include <array>
#include <concepts>
#include <fcntl.h>
#include <pthread.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <stdexcept>
#include <unistd.h>
#include <utility>
#include <vector>

#include "TCPSocket.h"
#include "utils/spscqueue.h"
#include "utils/MagicBuffer.h"

namespace network {

template <typename T>
concept HasOnDataMethod = requires(T obj, int fd, utils::MagicBuffer& buf) { 
    { obj.on_data(fd, buf) } -> std::same_as<void>;
};

template <HasOnDataMethod MessageHandler>
class EpollReactor {
public:
    EpollReactor(MessageHandler& handler, utils::SPSCQueue<int, 1024>& new_connections_queue,
                 int event_fd, bool initialize_buffers) 
        : handler_ { handler }
        , incoming_connections_ { new_connections_queue }
        , epoll_fd_ { epoll_create1(EPOLL_CLOEXEC) }
        , event_fd_ { event_fd } {
        if (epoll_fd_ == -1) [[unlikely]] {
            throw std::runtime_error("EpollReactor(): Unable to create epoll instance. Error: " + std::string(strerror(errno)));
        }

        // initialize magic buffers if thread is used to obtain market data
        if (initialize_buffers) {
            fd_buffers_.reserve(MAX_CONNECTIONS);
            for (int i = 0; i < MAX_CONNECTIONS; ++i) {
                fd_buffers_.emplace_back(MAX_BUFFER_SIZE);
            }
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
            throw std::runtime_error("EpollReactor::add_socket(): File descriptor passed in for add_socket is invalid.");
        }

        struct epoll_event event{};
        // edge triggered for receiving inputs
        event.events = EPOLLIN | EPOLLET;
        event.data.fd = fd;

        int rc = epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &event);
        if (rc == -1) {
            throw std::runtime_error("EpollReactor::add_socket(): Unable to add file descriptor to epoll interest list. Error: " + std::string(strerror(errno)));
        }
    }

    /*
    * One server thread will run this in order to receive incoming connections
    * This is to ensure that new connections will not interrupt the thread
    * that may be in the midst of processing incoming market data
    */
    void wait_for_connections(TCPSocket& server_socket) {
        // pin thread to Core 0 (beside main epoll thread on core 1)
        cpu_set_t cpu_set;
        CPU_ZERO(&cpu_set);
        CPU_SET(0, &cpu_set);
        pthread_t current_thread = pthread_self();
        int ret = pthread_setaffinity_np(current_thread, sizeof(cpu_set_t),  &cpu_set);
        if (ret != 0) [[unlikely]] {
            throw std::runtime_error("EpollReactor::wait_for_connections(): Unable to pin connections thread to Core 0. Error Code: " + std::to_string(ret));
        }

        while (true) {
            int new_connection_fd = server_socket.accept();
            if (new_connection_fd == -1) continue;

            while (!incoming_connections_.push(new_connection_fd));
            uint64_t dummy = 1; // used to signal a connection has been added
            int bytes_written = write(event_fd_, &dummy, sizeof(uint64_t));
            if (bytes_written == -1) [[unlikely]] {
                throw std::runtime_error("EpollReactor::wait_for_connections(): Unable to write to event_fd for new connection. Error: " + std::string(strerror(errno)));
            }
        }
    }

    /*
    * Main server thread will run this in order to accept market data
    * into the MagicBuffer, which can be then parsed into an Order to be
    * handled by the consumer thread for the order book
    */
    void run() {
        while (true) {
            struct epoll_event events[MAX_EVENTS];
            int ready_count = epoll_wait(epoll_fd_, events, MAX_EVENTS, -1); // -1 to block until an event occurs

            if (ready_count == -1) {
                if (errno == EINTR) {
                    continue;
                }
                throw std::runtime_error("EpollReactor::run(): Unexpected error occurred during epoll_wait. Error: " + std::string(strerror(errno)));
            }
    
            bool has_new_connections = false;
            for (int i = 0; i < ready_count; ++i) {
                int fd = events[i].data.fd;
                
                // new connections will be processed later so that we process market data first which is more important
                if (fd == event_fd_) {
                    has_new_connections = true;
                    continue;
                }

                // Note: make sure to read all of the data as we are using edge triggered. See man page for details
                while (true) {
                    utils::MagicBuffer& buffer = fd_buffers_[fd];
                    int bytes_received = recv(fd, buffer.get_write_head(), buffer.write_space_left(), 0);
                    if (bytes_received == -1) {
                        if (errno == EAGAIN || errno == EWOULDBLOCK) {
                            // no more data left in the buffer
                            break;
                        }
                        // unexpected error, close the client file descriptor
                        buffer.reset();
                        close(fd);
                        break;
                    } else if (bytes_received == 0) {
                        // client disconnected
                        buffer.reset(); // reset buffer for next file descriptor use
                        close(fd);
                        break;
                    } else {
                        // process data
                        handler_.on_data(fd, buffer);
                        buffer.advance_write(bytes_received);
                    }
                }
            }

            // process all new client connections after processing market data
            if (has_new_connections) {
                int new_connection_fd;
                while (incoming_connections_.pop(new_connection_fd)) {
                    int client_fd_flags = fcntl(new_connection_fd, F_GETFL, 0);
                    if (client_fd_flags == -1 || fcntl(new_connection_fd, F_SETFL, client_fd_flags | O_NONBLOCK) == -1) {
                        close(new_connection_fd);
                        continue;
                    };
                    add_socket(new_connection_fd);
                }
                uint64_t dummy;
                int rc = read(event_fd_, &dummy, sizeof(uint64_t));
                
                if (rc == -1) [[unlikely]] {
                    throw std::runtime_error("EpollReactor::run(): Unexpected error occured when clearing event_fd. Error: " + std::string(strerror(errno)));
                }
            }
        }
    }

    int get_epollfd() { return epoll_fd_; }

private:
    constexpr static const int MAX_EVENTS { 64 };
    constexpr static const int MAX_BUFFER_SIZE { 4096 }; // should be tuned accordingly (align with page size)
    constexpr static const int MAX_CONNECTIONS { 1000 }; // should be tuned when we have more information    

    MessageHandler& handler_;
    std::vector<utils::MagicBuffer> fd_buffers_;
    utils::SPSCQueue<int, 1024>& incoming_connections_; // 1024 is chosen as an arbitrary number. may want to tune if accepting new connections is a bottlneck.
    int epoll_fd_ { -1 };
    int event_fd_;

    using TCPSocket = network::TCPSocket;
};

}