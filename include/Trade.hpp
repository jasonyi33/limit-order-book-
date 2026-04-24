#pragma once

#include <cstdint>

struct Trade {
    uint64_t buy_order_id;
    uint64_t sell_order_id;
    int price;
    int quantity;
    uint64_t timestamp;
};
