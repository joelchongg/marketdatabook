#pragma once

#include <cstdint>

namespace protocol {

// used to replace std::visit with a jump table
enum class OrderType : uint8_t {
    AddOrder,
    ExecuteOrder,
    CancelOrder
};

// The header struct consists of all fields that are common to all order types
struct Header {
    uint64_t timestamp;
    uint64_t order_reference_number;
    uint16_t stock_locate;
    uint16_t tracking_number;
};

// AddOrder Struct is used for NASDAQ Itch Parsing (Section 1.3.1)
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

// Same struct as AddOrder, but with padding and in Host Byte Order
// reordered to minimize padding
struct NormalizedAddOrder {
    uint64_t timestamp;
    uint64_t order_reference_number;
    uint32_t shares;
    uint32_t price;
    uint16_t stock_locate;
    uint16_t tracking_number;
    char stock[8];
    char indicator;
};

// Section 1.4.1
struct OrderExecuted {
    char message_type;
    uint16_t stock_locate;
    uint16_t tracking_number;
    uint8_t timestamp[6];
    uint64_t order_reference_number;
    uint32_t shares;
    uint64_t match_number;
} __attribute__((packed));

// Same struct as OrderExecuted, but with padding and in Host Byte Order
// reordered to eliminate padding, message type is not needed as it can be inferred
struct NormalizedOrderExecuted {
    uint64_t timestamp;
    uint64_t order_reference_number;
    uint64_t match_number; 
    uint32_t shares;
    uint16_t stock_locate;
    uint16_t tracking_number;
};

// Section 1.4.3
struct CancelOrder {
    char message_type;
    uint16_t stock_locate;
    uint16_t tracking_number;
    uint8_t timestamp[6];
    uint64_t order_reference_number;
    uint32_t shares;
} __attribute__((packed));

// Same struct as CancelOrder, but with padding and in Host Byte Order
// reordered to eliminate padding, message type not needed as it can be inferred
struct NormalizedCancelOrder {
    uint64_t timestamp;
    uint64_t order_reference_number;
    uint32_t shares;
    uint16_t stock_locate;
    uint16_t tracking_number;
};

struct NormalizedOrder {
    OrderType type;
    union {
        NormalizedAddOrder add_order;
        NormalizedOrderExecuted execute_order;
        NormalizedCancelOrder cancel_order;
    };
};

static_assert(sizeof(AddOrder) == 36, "AddOrder is not packed into 36 bytes");
static_assert(sizeof(OrderExecuted) == 31, "OrderExecuted is not packed into 31 bytes");
static_assert(sizeof(CancelOrder) == 23, "CancelOrder is not packed into 23 bytes");

} // namespace protocol