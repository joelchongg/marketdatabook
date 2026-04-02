#pragma once

#include <cstdint>
#include <iostream> // for debugging

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

class ItchParser {
public:
    int on_data(int connection_fd, void* data, int bytes_available) {
        if (bytes_available < 36) { // equivalent to the size of an AddOrder struct
            // debugging
            std::cout << "Not enough bytes in data\n";
            return 0;
        }

        AddOrder* new_order = reinterpret_cast<AddOrder*>(data);
        // Itch data comes in big endian for Network Byte Order, swap it to little endian for x86
        new_order->order_reference_number = __builtin_bswap64(new_order->order_reference_number);
        new_order->shares = __builtin_bswap32(new_order->shares);
        new_order->price = __builtin_bswap32(new_order->price);

        // debugging
        std::cout << "New Order Reference Number: " << new_order->order_reference_number << '\n';
        std::cout << "New Order Shares: " << new_order->shares << '\n';
        std::cout << "New Order Price: " << new_order->price << '\n';
        std::cout << "For file descriptor: " << connection_fd << '\n';

        return 36;
    }
};

} // namespace protocol