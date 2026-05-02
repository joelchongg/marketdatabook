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
    ItchParser(utils::SPSCQueue<T, QUEUE_SIZE>& queue)
        : queue_ { queue }
        , logger_ { "logs/event_log.txt "}
    { }

    void on_data([[maybe_unused]] int connection_fd, utils::MagicBuffer& raw_data_buffer) {
        while (true) {
            char* raw_data = raw_data_buffer.get_read_head();

            // no more data left in the buffer
            if (raw_data_buffer.read_space_left() == 0) {
                return;
            }

            const char message_type = *raw_data;
            int bytes_available = raw_data_buffer.read_space_left();

            switch (message_type) {
                case 'A': if (bytes_available < 36) return; logger_.append(raw_data, 36); break;
                case 'E': if (bytes_available < 31) return; logger_.append(raw_data, 31); break;
                case 'X': if (bytes_available < 23) return; logger_.append(raw_data, 23); break;
                default: throw std::runtime_error("ItchParser: Current message type is not supported.");
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

            switch (message_type) {
                case 'A': parse_add_order(raw_data, header, raw_data_buffer); break;
                case 'E': parse_executed_order(raw_data, header, raw_data_buffer); break;
                case 'X': parse_cancel_order(raw_data, header, raw_data_buffer); break;
                default: throw std::runtime_error("ItchParser: Current order type is not supported and cannot be parsed.");
            }
        }
    }

private:
    utils::SPSCQueue<T, QUEUE_SIZE>& queue_;
    alignas(64) utils::MmapLogger logger_;

    void parse_add_order(const void* raw_data, Header& header, utils::MagicBuffer& buffer) {
        const AddOrder* data = reinterpret_cast<const AddOrder*>(raw_data);

        NormalizedAddOrder new_order;
        new_order.timestamp = header.timestamp;
        new_order.order_reference_number = header.order_reference_number;
        new_order.shares = __builtin_bswap32(data->shares);
        new_order.price = __builtin_bswap32(data->price);
        std::memcpy(&new_order.stock, &data->stock, 8);
        new_order.indicator = data->indicator;

        queue_.push(std::move(new_order));
        buffer.advance_read(36);
    }

    void parse_executed_order(const void* raw_data, Header& header, utils::MagicBuffer& buffer) {
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
        buffer.advance_read(31);
    }

    void parse_cancel_order(const void* raw_data, Header& header, utils::MagicBuffer& buffer) {
        const CancelOrder* data = reinterpret_cast<const CancelOrder*>(raw_data);

        NormalizedCancelOrder new_order;
        new_order.timestamp = header.timestamp;
        new_order.order_reference_number = header.order_reference_number;
        new_order.shares = __builtin_bswap32(data->shares);
        new_order.stock_locate = header.stock_locate;
        new_order.tracking_number = header.tracking_number;

        queue_.push(std::move(new_order));
        buffer.advance_read(23);
    }
};

} // namespace protocol