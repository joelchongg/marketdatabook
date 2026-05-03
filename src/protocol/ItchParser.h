#pragma once

#include "OrderTypes.h"
#include "utils/MagicBuffer.h"
#include "utils/MmapLogger.h"
#include "utils/spscqueue.h"

#include <cstdint>
#include <cstring>
#include <iostream> // for debugging, remove eventually

namespace protocol {

template <typename T, size_t QUEUE_SIZE>
class ItchParser {
public:
    ItchParser(utils::SPSCQueue<T, QUEUE_SIZE>& queue, utils::MmapLogger& logger)
        : queue_ { queue }
        , logger_ { logger }
    { }

    void parse_block(char* payload_ptr, int payload_len) {
        while (true) {
            // should be well predicted
            if (payload_len == 0) {
                return;
            }

            const char message_type = *payload_ptr;
            uint8_t message_len = message_length_table[message_type - '\0'];
            if (payload_len < message_len) {
                return;
            }

            switch (message_type) {
                case 'A': 
                    logger_.append(payload_ptr, 36); 
                    break;
                case 'E':
                    logger_.append(payload_ptr, 31);
                    break;
                case 'X':
                    logger_.append(payload_ptr, 23);
                    break;
                default:
                    // currently unsupported order messages
                    logger_.append(payload_ptr, message_len);
                    payload_ptr += message_len;
                    payload_len -= message_len;
                    continue;
            }

            Header header;

            uint64_t ts = 0;
            std::memcpy(&ts, payload_ptr + 5, 6);
            header.timestamp = __builtin_bswap64(ts) >> 16;

            std::memcpy(&header.order_reference_number, payload_ptr + 11, 8);
            header.order_reference_number = __builtin_bswap64(header.order_reference_number);

            std::memcpy(&header.stock_locate, payload_ptr + 1, 2);
            header.stock_locate = __builtin_bswap16(header.stock_locate);

            std::memcpy(&header.tracking_number, payload_ptr + 3, 2);
            header.tracking_number = __builtin_bswap16(header.tracking_number);

            switch (message_type) {
                case 'A':
                    parse_add_order(payload_ptr, header);
                    payload_ptr += 36;
                    payload_len -= 36;
                    break;
                case 'E':
                    parse_executed_order(payload_ptr, header);
                    payload_ptr += 31;
                    payload_len -= 31;
                    break;
                case 'X':
                    parse_cancel_order(payload_ptr, header);
                    payload_ptr += 23;
                    payload_len -= 23;
                    break;
            }
        }
    }

private:
    constexpr static const uint8_t message_length_table[256] = {
        0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
        0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
        0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
        0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
        0,  36,  19,  36,  19,  31,  40,   0,  25,  50,  35,  28,  26,   0,  20,  48,
       44,  40,  39,  12,   0,  35,  35,  12,  23,  20,   0,   0,   0,   0,   0,   0,
        0,   0,   0,   0,   0,   0,   0,   0,  21,   0,   0,   0,   0,   0,   0,   0,
        0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
        0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
        0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
        0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
        0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
        0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
        0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
        0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
        0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    };

    utils::SPSCQueue<T, QUEUE_SIZE>& queue_;
    utils::MmapLogger& logger_;

    void parse_add_order(const void* raw_data, Header& header) {
        const AddOrder* data = reinterpret_cast<const AddOrder*>(raw_data);

        NormalizedAddOrder new_order;
        new_order.timestamp = header.timestamp;
        new_order.order_reference_number = header.order_reference_number;
        new_order.shares = __builtin_bswap32(data->shares);
        new_order.price = __builtin_bswap32(data->price);
        std::memcpy(&new_order.stock, &data->stock, 8);
        new_order.indicator = data->indicator;

        queue_.push(std::move(new_order));
    }

    void parse_executed_order(const void* raw_data, Header& header) {
        const OrderExecuted* data = reinterpret_cast<const OrderExecuted*>(raw_data);
        
        // check if AVX instructions can be used
        NormalizedOrderExecuted new_order;
        new_order.timestamp = header.timestamp;
        new_order.order_reference_number = header.order_reference_number;
        new_order.match_number = __builtin_bswap64(data->match_number);
        new_order.shares = __builtin_bswap32(data->shares);
        new_order.stock_locate = header.stock_locate;
        new_order.tracking_number = header.tracking_number;

        queue_.push(std::move(new_order));
    }

    void parse_cancel_order(const void* raw_data, Header& header) {
        const CancelOrder* data = reinterpret_cast<const CancelOrder*>(raw_data);

        NormalizedCancelOrder new_order;
        new_order.timestamp = header.timestamp;
        new_order.order_reference_number = header.order_reference_number;
        new_order.shares = __builtin_bswap32(data->shares);
        new_order.stock_locate = header.stock_locate;
        new_order.tracking_number = header.tracking_number;

        queue_.push(std::move(new_order));
    }
};

} // namespace protocol