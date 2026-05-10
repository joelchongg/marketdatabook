#pragma once

#include <cstdint>

namespace protocol {

// Specification used: https://nasdaqtrader.com/content/technicalsupport/specifications/TradingProducts/Ouch5.0.pdf

struct EnterOrder {
    uint8_t type;
    uint32_t user_ref_num;
    uint8_t side;
    uint32_t quantity;
    uint64_t symbol;
    uint64_t price;
    uint8_t time_in_force;
    uint8_t display;
    uint8_t capacity;
    uint8_t intermarket_sweep_eligibility;
    uint8_t cross_type;
    uint8_t cust_order_id[14];
    uint16_t appendage_len;
    // optional appendage of varying len can be added
} __attribute__((packed));


struct FramedEnterOrder {
    uint16_t soup_length;
    uint8_t soup_type;
    uint8_t type;
    uint32_t user_ref_num;
    uint8_t side;
    uint32_t quantity;
    uint64_t symbol;
    uint64_t price;
    uint8_t time_in_force;
    uint8_t display;
    uint8_t capacity;
    uint8_t intermarket_sweep_eligibility;
    uint8_t cross_type;
    uint8_t cust_order_id[14];
    uint16_t appendage_len;
    // optional appendage of varying len can be added
} __attribute__((packed));

static_assert(sizeof(EnterOrder) == 47, "Enter Order is not packed into 47 bytes (excludes optional appendage)");
static_assert(sizeof(FramedEnterOrder) == 50, "Framed Enter order is not packed into 50 bytes (excludes optional appendage)");

} // namespace protocol