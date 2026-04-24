#pragma once

#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>

enum class Side {
    Buy,
    Sell
};

enum class OrderType {
    Limit,
    Market
};

struct Order {
    uint64_t id;
    Side side;
    OrderType type;
    int price;
    int quantity;
    uint64_t timestamp;
};

inline std::string sideToString(Side side) {
    switch (side) {
    case Side::Buy:
        return "BUY";
    case Side::Sell:
        return "SELL";
    }

    throw std::invalid_argument("Unknown side");
}

inline std::string orderTypeToString(OrderType type) {
    switch (type) {
    case OrderType::Limit:
        return "LIMIT";
    case OrderType::Market:
        return "MARKET";
    }

    throw std::invalid_argument("Unknown order type");
}

inline std::string formatPrice(int cents) {
    if (cents < 0) {
        return "N/A";
    }

    const int dollars = cents / 100;
    const int remainder = std::abs(cents % 100);

    std::ostringstream stream;
    stream << dollars << '.' << std::setw(2) << std::setfill('0') << remainder;
    return stream.str();
}
