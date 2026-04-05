#pragma once

#include "OrderTypes.h"
#include "utils/spscqueue.h"

#include <cstdint>
#include <cstring>

#include <iostream> // for debugging
namespace protocol {

template <typename T, size_t QUEUE_SIZE>
class ItchParser {
public:
    ItchParser(utils::SPSCQueue<T, QUEUE_SIZE>& queue)
        : queue_ { queue }
    { }

    int on_data(int connection_fd, void* raw_data_buffer, int bytes_available) {
        char* raw_data = reinterpret_cast<char *>(raw_data_buffer);

        const char message_type = *raw_data;

        switch (message_type) {
            case 'A': if (bytes_available < 36) return 0; break;
            case 'E': if (bytes_available < 31) return 0; break;
            case 'X': if (bytes_available < 23) return 0; break;
        }

        Header header;

        uint64_t ts = 0;
        std::memcpy(&ts, raw_data + 5, 6);
        header.timestamp = __builtin_bswap64(ts) >> 16;

        std::memcpy(&header.order_reference_number, raw_data + 11, 8);
        header.order_reference_number = __builtin_bswap64(header.order_reference_number);

        std::memcpy(&header.stock_locate, raw_data + 1, 2);
        header.stock_locate = __builtin_bswap16(header.stock_locate);

        std::memcpy(&header.tracking_number, raw_data + 3, 2);
        header.tracking_number = __builtin_bswap16(header.tracking_number);
        
        // do to avoid unused parameter warning for now
        std::cout << "Processing connection_fd: " << connection_fd << '\n';

        switch (message_type) {
            case 'A': return parse_add_order(raw_data, header);
            case 'E': return parse_executed_order(raw_data, header);
            case 'X': return parse_cancel_order(raw_data, header);
            default: return 0;
        }

    }

private:
    utils::SPSCQueue<T, QUEUE_SIZE>& queue_;

    int parse_add_order(const void* raw_data, Header& header) {
        const AddOrder* data = reinterpret_cast<const AddOrder*>(raw_data);

        NormalizedAddOrder new_order;
        new_order.timestamp = header.timestamp;
        new_order.order_reference_number = header.order_reference_number;
        new_order.shares = __builtin_bswap32(data->shares);
        new_order.price = __builtin_bswap32(data->price);
        std::memcpy(&new_order.stock, &data->stock, 8);
        new_order.indicator = data->indicator;

        queue_.push(std::move(new_order));

        return 36;
    }

    int parse_executed_order(const void* raw_data, Header& header) {
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

        return 31;
    }

    int parse_cancel_order(const void* raw_data, Header& header) {
        const CancelOrder* data = reinterpret_cast<const CancelOrder*>(raw_data);

        NormalizedCancelOrder new_order;
        new_order.timestamp = header.timestamp;
        new_order.order_reference_number = header.order_reference_number;
        new_order.shares = __builtin_bswap32(data->shares);
        new_order.stock_locate = header.stock_locate;
        new_order.tracking_number = header.tracking_number;

        queue_.push(std::move(new_order));

        return 23;
    }
};

} // namespace protocol