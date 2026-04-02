#pragma once

#include "utils/spscqueue.h"

#include <cstdint>

namespace protocol {

// AddOrder Struct is used for NASDAQ Itch Parsing (Using Section 1.3.1)
// https://www.nasdaqtrader.com/content/technicalsupport/specifications/dataproducts/NQTVITCHSpecification.pdf
struct AddOrder {
    char message_type;
    uint16_t stock_locate;
    uint16_t tracking_number;
    uint8_t timestamp[6];
    uint64_t order_reference_number;
    char indicator;
    uint32_t shares;
    char stock[8];
    uint32_t price;
} __attribute__((packed));

template <size_t QUEUE_SIZE>
class ItchParser {
public:
    ItchParser(utils::SPSCQueue<protocol::AddOrder, QUEUE_SIZE>& queue)
        : queue_ { queue }
    { }

    int on_data(int connection_fd, void* data, int bytes_available) {
        if (bytes_available < 36) { // equivalent to the size of an AddOrder struct
            return 0;
        }

        AddOrder* new_order = reinterpret_cast<AddOrder*>(data);
        // Itch data comes in big endian for Network Byte Order, swap it to little endian for x86
        new_order->order_reference_number = __builtin_bswap64(new_order->order_reference_number);
        new_order->shares = __builtin_bswap32(new_order->shares);
        new_order->price = __builtin_bswap32(new_order->price);

        queue_.push(std::move(*new_order));
        return 36;
    }

private:
    utils::SPSCQueue<AddOrder, QUEUE_SIZE>& queue_;
};

} // namespace protocol