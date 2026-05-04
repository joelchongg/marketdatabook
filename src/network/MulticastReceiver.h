#pragma once

#include "utils/PCAPSimulation.h"

#include <arpa/inet.h>
#include <atomic>
#include <cstring>
#include <emmintrin.h>
#include <fcntl.h>
#include <iostream>
#include <linux/filter.h>
#include <linux/if_ether.h>
#include <linux/if_packet.h>
#include <net/if.h>
#include <pthread.h>
#include <sched.h>
#include <stdexcept>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
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

        // setup cBPF filter to filter ARP or unwanted multicast packets
        struct sock_fprog bpf;
        bpf.len = sizeof(bpf_code) / sizeof(bpf_code[0]);
        bpf.filter = bpf_code;
        ret = setsockopt(fd_, SOL_PACKET, SO_ATTACH_FILTER, &bpf, sizeof(bpf));
        if (ret == -1) [[unlikely]] {
            close(fd_);
            throw std::runtime_error("MulticastReceiver: Unable to attach BPF filter. Error: " + std::string(strerror(errno)));
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
    }

    /*
    * Used for simulating multicast due to lack of permissions
    */
    MulticastReceiver(MessageHandler& handler, bool mock_mode)
        : handler_ { handler } {
        if (mock_mode) [[likely]] {
            total_size_ = RX_BLOCK_SIZE * RX_BLOCK_NR;
            void* ring_addr = mmap(NULL, total_size_, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE, -1, 0);
            if (ring_addr == MAP_FAILED) [[unlikely]] {
                throw std::runtime_error("MulticastReceiver: Unable to create mock RX ring buffer. Error: " + std::string(strerror(errno)));
            }
            ring_ = static_cast<char *>(ring_addr);
        } else {
            throw std::runtime_error("MulticastReceiver: Mock mode constructor should only be used when mock_mode is true.");
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
            int payload_length = packet->tp_snaplen - ETH_HLEN - 20 - 8;
            handler_.parse_block(payload_ptr, payload_length);

            // return frame to kernel
            status.store(TP_STATUS_KERNEL, std::memory_order_release);
            curr_frame_idx_ = (curr_frame_idx_ + 1) % RX_FRAME_NR;
            total_packets.fetch_add(1, std::memory_order_relaxed);
        }
    }

    /*
    * This function is used to obtain statistics regarding received and dropped packets
    * This function is used to ensure that packets are not dropped and we can process packets at an optimal speed.
    */
    void print_statistics() const {
        // pin current thread so that it does not migrate to the main cores
        cpu_set_t cpu_set;
        CPU_ZERO(&cpu_set);
        CPU_SET(7, &cpu_set);

        pthread_t current_thread = pthread_self();
        if (int ret = pthread_setaffinity_np(current_thread, sizeof(cpu_set_t), &cpu_set); ret != 0) {
            std::cout << "Unable to pin statistics thread to a separate core. Error Code: " << ret << '\n';
        }

        while (true) {
            struct tpacket_stats stats;
            socklen_t len = sizeof(stats);
            if (getsockopt(fd_, SOL_PACKET, PACKET_STATISTICS, &stats, &len) == -1) {
                std::cout << "Unable to get packet statistics. Error: " << strerror(errno) << '\n';
                return;
            }
    
            printf("[Telemetry] Packets Per Second: %d | Drops Per Second: %d\n", stats.tp_packets, stats.tp_drops);
            sleep(1);
        }
    }

    void print_statistics_mock_mode() {
        // pin current thread so that it does not migrate to the main cores
        cpu_set_t cpu_set;
        CPU_ZERO(&cpu_set);
        CPU_SET(7, &cpu_set);

        pthread_t current_thread = pthread_self();
        if (int ret = pthread_setaffinity_np(current_thread, sizeof(cpu_set_t), &cpu_set); ret != 0) {
            std::cout << "Unable to pin statistics thread to a separate core. Error Code: " << ret << '\n';
        }

        uint64_t packets_per_second;
        while (true) {
            packets_per_second = total_packets.exchange(0, std::memory_order_relaxed);
            printf("[Telemetry (Mock Mode)] Packets Per Second: %ld\n", packets_per_second);
            sleep(1);
        }
    }

    /*
    * Used to simulate packets by using a PCAP file for replay without tcpreplay
    * A thread runs this to add packets to the ring buffer to simulate the kernel
    */
    void simulate_packets(const std::string& filename) {
        // pin current thread to core 0 which is beside the main poller thread on core 1
        cpu_set_t cpu_set;
        CPU_ZERO(&cpu_set);
        CPU_SET(0, &cpu_set);

        pthread_t current_thread = pthread_self();
        if (int ret = pthread_setaffinity_np(current_thread, sizeof(cpu_set_t), &cpu_set); ret != 0) {
            std::cout << "Unable to pin simulate_packets thread to core 0. Error Code: " << ret << '\n';
        }

        int fd = open(filename.c_str(), O_RDONLY);
        if (fd == -1) [[unlikely]] {
            throw std::runtime_error("MulticastReceiver: Unable to open file " + filename + " to simulate packet arrival. Error: " + std::string(strerror(errno)));
        }

        struct stat buf;
        fstat(fd, &buf);
        void* data_addr = mmap(NULL, buf.st_size, PROT_READ, MAP_PRIVATE | MAP_POPULATE, fd, 0);
        if (data_addr == MAP_FAILED) [[unlikely]] {
            throw std::runtime_error("MulticastReceiver: Unable to mmap for file " + filename + ". Error: " + std::string(strerror(errno)));
        }

        char* data = static_cast<char *>(data_addr);
        char* data_end = data + buf.st_size;
        if (madvise(data, buf.st_size, MADV_SEQUENTIAL) == -1) [[unlikely]] {
            throw std::runtime_error("MulticastReceiver: Unable to set MADV_SEQUENTIAL for simulating packets. Error: " + std::string(strerror(errno)));
        }

        // skip global header
        data += sizeof(utils::pcap_global_hdr);
        utils::pcap_packet_hdr* packet_hdr_ptr = reinterpret_cast<utils::pcap_packet_hdr *>(data);
        int frame_idx = 0;
        while (data < data_end) {
            char* curr_frame_ptr = ring_ + (frame_idx * RX_FRAME_SIZE);
            struct tpacket2_hdr* frame_hdr_ptr = reinterpret_cast<struct tpacket2_hdr *>(curr_frame_ptr);
            std::atomic_ref<unsigned int> status(frame_hdr_ptr->tp_status);
            while (status.load(std::memory_order_acquire) & TP_STATUS_USER) { _mm_pause(); }

            // set frame header information
            frame_hdr_ptr->tp_sec = packet_hdr_ptr->ts_sec;
            frame_hdr_ptr->tp_nsec = packet_hdr_ptr->ts_usec * 1000;
            frame_hdr_ptr->tp_snaplen = packet_hdr_ptr->incl_len;
            frame_hdr_ptr->tp_len = packet_hdr_ptr->orig_len;
            frame_hdr_ptr->tp_mac = TPACKET_ALIGN(sizeof(struct tpacket2_hdr));
            frame_hdr_ptr->tp_net = frame_hdr_ptr->tp_mac + ETH_HLEN;

            // set payload
            if (data + sizeof(utils::pcap_packet_hdr) + packet_hdr_ptr->incl_len > data_end) break;
            std::memcpy(curr_frame_ptr + frame_hdr_ptr->tp_mac, data + sizeof(utils::pcap_packet_hdr), packet_hdr_ptr->incl_len); 

            status.store(TP_STATUS_USER, std::memory_order_release);
            data += sizeof(utils::pcap_packet_hdr) + packet_hdr_ptr->incl_len;
            packet_hdr_ptr = reinterpret_cast<utils::pcap_packet_hdr *>(data);
            frame_idx = (frame_idx + 1) % RX_FRAME_NR;
        }

        close(fd);
        munmap(data_addr, buf.st_size);
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
    alignas(64) std::atomic<uint64_t> total_packets{0}; // used during mock mode to check throughput

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