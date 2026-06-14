#pragma once

#include <cstdint>

namespace recovery {

/*
* WalFrame is used for deterministic recovery, 
* containing fields for error checking in case of torn writes
*/
struct alignas(64) WalFrame {
    uint8_t marker;         // 0xAA for a valid frame, since fallocated WAL has 0x00 bytes pre-written, and 0xFF is read if SSD fails
    uint8_t msg_type;       // ITCH Protocol Message Type
    uint16_t channel_id;    // Used to determine which feed / NIC the data is from, in case of A/B arbitration
    uint32_t seq_num;
    uint64_t timestamp;
    uint8_t payload[44];
    uint32_t checksum;
};

} // namespace recovery