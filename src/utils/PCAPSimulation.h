#pragma once

#include <cstdint>

namespace utils {

// 24-byte Global Header (Appears exactly once at the start of the file)
struct pcap_global_hdr {
    uint32_t magic_number;  // 0xa1b2c3d4 implies native endianness
    uint16_t major_version;
    uint16_t minor_version;
    int32_t thiszone;
    uint32_t sigfigs;
    uint32_t snaplen;
    uint32_t network;       // 1 = Ethernet
};

// 16-byte Packet Header (Appears before EVERY packet)
struct pcap_packet_hdr {
    uint32_t ts_sec;   // Timestamp seconds
    uint32_t ts_usec;  // Timestamp microseconds
    uint32_t incl_len; // Number of octets of packet saved in file
    uint32_t orig_len; // Actual length of packet
};

} // namespace utils