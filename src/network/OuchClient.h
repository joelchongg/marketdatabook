#pragma once

#include "protocol/OuchOrderTypes.h"
#include "TCPSocket.h"

#include <sys/uio.h>

namespace network {

/*
* Union is used due to OUCH having different order types
* Avoids overhead of std::variant, which may cause us to send the type id as well as data payload.
*/
union OuchPayload {
    protocol::FramedEnterOrder enter_order;
};

/*
* Used to store metadata along with the OuchPayload union object
* Metadata allows us to know how much data to send, without querying for the actual type of the data in the union
* Aligned to 64 bytes to avoid false sharing as the OUCH order pool will be shared amongst all OuchClient objects
*/
struct alignas(64) OuchPacket {
    size_t packet_len;
    OuchPayload payload;
};

/*
* The OuchClient class manages the outbound TCP connection to the exchange.
* This is used to send orders to the exchange.
*/
class OuchClient {
private:
    enum class OuchState {
        Disconnected,
        Connecting,
        Connected,
        PendingWrite
    };

public:
    OuchClient(const std::string& port_number) 
        : socket_(port_number), socket_fd_ { socket_.get_fd() } { 
        socket_.set_non_blocking();
        socket_.set_no_delay(1);
    }

    /*
    * Used to connect to the NASDAQ OUCH Server.
    * server_name can be either IP Address or the human readable URL.
    * Returns the return value of calling connect() from the TCPSocket class.
    */
    int connect_to_server(const std::string& server_name, const std::string& port_number) {
        state_ = OuchState::Connecting;
        return socket_.connect(server_name, port_number);
    }

    /*
    * Used to send orders to the NASDAQ OUCH Server.
    * Returns false if orders are not sent completely.
    */
    bool send_order(OuchPacket* order) {
        // do not send a new order if there is a pending write in order to prevent corruption
        if (state_ == OuchState::PendingWrite) {
            return false;
        }

        size_t packet_len = order->packet_len;
        ssize_t bytes_written = send(socket_fd_, &order->payload, packet_len, 0);
        if (bytes_written == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                bytes_written = 0;
            } else [[unlikely]] {
                throw std::runtime_error("OuchClient::send_order: Unable to send order to server. Error: " + std::string(strerror(errno)));
            }
        }

        // half write occurred, EpollReactor will call handle_write in order to send the rest of the packet
        if (bytes_written < packet_len) {
            state_ = OuchState::PendingWrite;
            pending_buffer_ = reinterpret_cast<const uint8_t*>(&order->payload) + bytes_written;
            pending_bytes_ =  packet_len - bytes_written;
            return false;
        }

        return true;
    }

    /*
    * Used by EpollReactor in order to finish half writes, or setting up of connection
    */
    bool handle_write() {
        // although it should be predicted well, use [[unlikely]] to move it out of the hot path for better pipelining
        if (state_ == OuchState::Connecting) [[unlikely]] {
            int error = 0;
            socklen_t len = sizeof(error);
            if (getsockopt(socket_fd_, SOL_SOCKET, SO_ERROR, &error, &len) == -1) [[unlikely]] {
                throw std::runtime_error("OuchClient::handle_write: Unable to retrieve socket options. Error: " + std::string(strerror(errno)));
            }
            if (error != 0) {
                throw std::runtime_error("OuchClient::handle_write: Error occured during connection establishment. Error: " + std::string(strerror(error)));
            }

            state_ = OuchState::Connected;
            return true;
        } else if (state_ == OuchState::PendingWrite) {
            ssize_t bytes_written = send(socket_fd_, pending_buffer_, pending_bytes_, 0);
            if (bytes_written == -1) [[unlikely]] {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    bytes_written = 0;
                } else [[unlikely]] {
                    throw std::runtime_error("OuchClient::handle_write: Unable to send order to server. Error: " + std::string(strerror(errno)));
                }
            }

            if (bytes_written < pending_bytes_) {
                pending_buffer_ = pending_buffer_ + bytes_written;
                pending_bytes_ -= bytes_written;
                return false;
            }

            state_ = OuchState::Connected;
            return true;
        }
        return true;
    }

private:
    TCPSocket socket_;
    int socket_fd_ { -1 };
    OuchState state_{OuchState::Disconnected};
    const uint8_t* pending_buffer_{nullptr};
    size_t pending_bytes_{0};
};

} // namespace network