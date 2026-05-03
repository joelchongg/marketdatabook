#pragma once

#include <arpa/inet.h>
#include <atomic>
#include <cstring>
#include <emmintrin.h>
#include <linux/filter.h>
#include <linux/if_ether.h>
#include <linux/if_packet.h>
#include <net/if.h>
#include <stdexcept>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <unistd.h>
#include <utility>

namespace network {

template <typename T>
concept HasParseBlockMethod = requires(T obj, char* payload_ptr, int payload_len) { 
    { obj.parse_block(payload_ptr, payload_len) } -> std::same_as<void>;
};

/*
* MulticastReceiver class is used to bypass recv() and epoll overhead for receiving multicast packets.
* This is done by using AF_PACKET to process raw packets.
* Packets are also filtered using cBPF to avoid unnecessary packets to be processed in user space.
*/
template <HasParseBlockMethod MessageHandler>
class MulticastReceiver {
public:
    /*
    * Takes in the interface name to receive multicast packets from, and the IP to receive multicast packets from
    */
    MulticastReceiver(MessageHandler& handler, const std::string& interface_name, const std::string& multicast_addr)
        : handler_ { handler } {
        fd_ = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
        if (fd_ == -1) [[unlikely]] {
            throw std::runtime_error("MulticastReceiver: Unable to create AF_PACKET socket. Error: " + std::string(strerror(errno)));
        }

        // add multicast packet membership
        
        // retreive interface index
        struct ifreq ifreq;
        std::memcpy(ifreq.ifr_ifrn.ifrn_name, interface_name.data(), interface_name.size() + 1); // account for null terminator
        if (ioctl(fd_, SIOCGIFINDEX, &ifreq) == -1) [[unlikely]] {
            close(fd_);
            throw std::runtime_error("MulticastReceiver: Unable to retrieve interface index for interface." + std::string(strerror(errno)));
        }
        int interface_idx = ifreq.ifr_ifindex;

        struct packet_mreq mreq;
        mreq.mr_ifindex = interface_idx;
        mreq.mr_type = PACKET_MR_MULTICAST;
        mreq.mr_alen = 6;
        uint32_t ip_addr;
        if (int ret = inet_pton(AF_INET, multicast_addr.data(), &ip_addr); ret == -1) [[unlikely]] {
            close(fd_);
            throw std::runtime_error("MulticastReceiver: Unable to retrieve integer network representation for multicast address. Error: " + std::string(strerror(errno)));
        } else if (ret == 0) [[unlikely]] {
            close(fd_);
            throw std::runtime_error("MulticastReceiver: Invalid network format.");
        }

        uint8_t* raw_address_bytes = reinterpret_cast<uint8_t*>(&ip_addr);
        // all multicast MAC addresses start with 01:00:5E
        mreq.mr_address[0] = 0x01;
        mreq.mr_address[1] = 0x00;
        mreq.mr_address[2] = 0x5E;
        mreq.mr_address[3] = raw_address_bytes[1] & 0x7F; // set MSB to 0

        int ret = setsockopt(fd_, SOL_PACKET, PACKET_ADD_MEMBERSHIP, &mreq, sizeof(mreq));
        if (ret == -1) [[unlikely]] {
            close(fd_);
            throw std::runtime_error("MulticastReceiver: Unable to add multicast packet membership. Error: " + std::string(strerror(errno)));
        }

        // use TPACKET_V2 instead of TPACKET_V3 to optimize for latency over throughput
        int tpacket_ver = TPACKET_V2;
        ret = setsockopt(fd_, SOL_PACKET, PACKET_VERSION, &tpacket_ver, sizeof(tpacket_ver));
        if (ret == -1) [[unlikely]] {
            close(fd_);
            throw std::runtime_error("MulticastReceiver: Unable to set TPACKET_VERSION. Error: " + std::string(strerror(errno)));
        }

        // setup RX ring
        struct tpacket_req rx_req;
        rx_req.tp_block_size = RX_BLOCK_SIZE;
        rx_req.tp_frame_size = RX_FRAME_SIZE;
        rx_req.tp_block_nr = RX_BLOCK_NR;
        rx_req.tp_frame_nr = RX_FRAME_NR;
        ret = setsockopt(fd_, SOL_PACKET, PACKET_RX_RING, &rx_req, sizeof(rx_req));
        if (ret == -1) [[unlikely]] {
            close(fd_);
            throw std::runtime_error("MulticastReceiver: Unable to setup RX ring. Error: " + std::string(strerror(errno)));
        }

        // create shared mmap between user space and kernel space for zero copy packets
        total_size_ = rx_req.tp_block_size * rx_req.tp_block_nr;
        void* ring_addr = mmap(NULL, total_size_, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE, fd_, 0);
        if (ring_addr == MAP_FAILED) [[unlikely]] {
            close(fd_);
            throw std::runtime_error("MulticastReceiver: Unable to mmap RX ring buffer. Error: " + std::string(strerror(errno)));
        }
        ring_ = static_cast<char *>(ring_addr);

        // setup cBPF filter to filter ARP or unwanted multicast packets
        struct sock_fprog bpf;
        bpf.len = sizeof(bpf_code) / sizeof(bpf_code[0]);
        bpf.filter = bpf_code;
        ret = setsockopt(fd_, SOL_PACKET, SO_ATTACH_FILTER, &bpf, sizeof(bpf));
        if (ret == -1) [[unlikely]] {
            close(fd_);
            munmap(ring_, total_size_);
            throw std::runtime_error("MulticastReceiver: Unable to attach BPF filter. Error: " + std::string(strerror(errno)));
        }
    }

    ~MulticastReceiver() {
        if (ring_) munmap(ring_, total_size_);
        if (fd_ != -1) close(fd_);
    }

    // disable copy semantics
    MulticastReceiver(const MulticastReceiver&) = delete;
    MulticastReceiver& operator=(const MulticastReceiver&) = delete;

    // allow move semantics
    MulticastReceiver(MulticastReceiver&& other)
        : ring_ { std::exchange(other.ring_, nullptr) }, fd_ { std::exchange(other.fd_, -1) }, total_size_ { other.total_size_ }
    { }

    MulticastReceiver& operator=(MulticastReceiver&& other) {
        if (&other == this) {
            return *this;
        }

        if (fd_) {
            close(fd_);
            munmap(ring_, total_size_);
        }

        ring_ = std::exchange(other.ring_, nullptr);
        fd_ = std::exchange(other.fd_, -1);
        total_size_ = other.total_size_;
    
        return *this;
    }

    /*
    * Used to repeatedly poll for incoming multicast data.
    * This function uses a while (true) loop to poll forever.
    */
    void poll() {
        while (true) {
            char* start_of_frame = ring_ + (curr_frame_idx_ * RX_FRAME_SIZE);
            struct tpacket2_hdr* packet = reinterpret_cast<struct tpacket2_hdr *>(start_of_frame);
            std::atomic_ref<unsigned int> status(packet->tp_status);
            while ((status.load(std::memory_order_acquire) & TP_STATUS_USER) == 0) { _mm_pause(); }

            // get start of data by pointer arithmetic past header bytes and parse data
            // IP Header 20 Bytes, UDP header 8 bytes
            char* payload_ptr = start_of_frame + packet->tp_net + 20 + 8;
            int payload_length = packet->tp_snaplen - packet->tp_net - 20 - 8;
            handler_.parse_block(payload_ptr, payload_length);

            // return frame to kernel
            status.store(TP_STATUS_KERNEL, std::memory_order_release);
            curr_frame_idx_ = (curr_frame_idx_ + 1) % RX_FRAME_NR;
        }
    }

private:
    constexpr static unsigned int RX_BLOCK_SIZE { 1 << 16 };    // 64 KB, can be increased when using HugePages
    constexpr static unsigned int RX_FRAME_SIZE { 2048 };       // nearest powero f 2 for MTU + TPACKET_HDRLEN
    constexpr static unsigned int RX_BLOCK_NR { 128 };          // arbitrary value, can be tuned
    constexpr static unsigned int RX_FRAME_NR { (RX_BLOCK_SIZE * RX_BLOCK_NR) / RX_FRAME_SIZE };

    MessageHandler& handler_;
    char* ring_ { nullptr };
    size_t curr_frame_idx_ { 0 };
    int fd_ { -1 };
    int total_size_ { 0 };

    // bpf filter code generated via tcpdump (tcpdump -dd "udp and dst port 58362")
    static inline struct sock_filter bpf_code[] = {
        { 0x28, 0, 0, 0x0000000c },
        { 0x15, 0, 7, 0x00000800 },
        { 0x30, 0, 0, 0x00000017 },
        { 0x15, 0, 11, 0x00000011 },
        { 0x28, 0, 0, 0x00000014 },
        { 0x45, 9, 0, 0x00001fff },
        { 0xb1, 0, 0, 0x0000000e },
        { 0x48, 0, 0, 0x00000010 },
        { 0x15, 5, 6, 0x0000e3fa },
        { 0x15, 0, 5, 0x000086dd },
        { 0x30, 0, 0, 0x00000014 },
        { 0x15, 0, 3, 0x00000011 },
        { 0x28, 0, 0, 0x00000038 },
        { 0x15, 0, 1, 0x0000e3fa },
        { 0x6, 0, 0, 0x00040000 },
        { 0x6, 0, 0, 0x00000000 }
    };
};

} // namespace network