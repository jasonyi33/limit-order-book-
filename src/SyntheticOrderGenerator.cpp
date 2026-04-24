#include "SyntheticOrderGenerator.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <random>
#include <stdexcept>
#include <unordered_map>
#include <vector>

#include "OrderBook.hpp"

namespace {

struct LiveOrderIndex {
    Order order;
    size_t index;
};

void removeLiveOrder(
    std::unordered_map<uint64_t, LiveOrderIndex>& live_orders,
    std::vector<uint64_t>& live_ids,
    uint64_t order_id
) {
    auto it = live_orders.find(order_id);
    if (it == live_orders.end()) {
        return;
    }

    const size_t index = it->second.index;
    const uint64_t tail_id = live_ids.back();
    live_ids[index] = tail_id;
    live_orders[tail_id].index = index;
    live_ids.pop_back();
    live_orders.erase(it);
}

int nextPositiveQuantity(std::mt19937_64& rng, int average_order_size) {
    std::normal_distribution<double> quantity_dist(
        static_cast<double>(average_order_size),
        std::max(1.0, static_cast<double>(average_order_size) / 2.0)
    );

    return std::max(1, static_cast<int>(std::llround(quantity_dist(rng))));
}

int nextReferencePrice(std::mt19937_64& rng, int current_price, int volatility) {
    if (volatility == 0) {
        return std::max(1, current_price);
    }

    std::normal_distribution<double> price_shift_dist(0.0, static_cast<double>(volatility));
    const int shifted = current_price + static_cast<int>(std::llround(price_shift_dist(rng)));
    return std::max(1, shifted);
}

int nextLimitPrice(std::mt19937_64& rng, int reference_price, int volatility, Side side) {
    const int max_offset = std::max(1, volatility * 3);
    std::uniform_int_distribution<int> offset_dist(0, max_offset);
    std::bernoulli_distribution aggressive_dist(0.25);

    const int offset = offset_dist(rng);
    const bool aggressive = aggressive_dist(rng);

    int price = reference_price;
    if (side == Side::Buy) {
        price += aggressive ? offset : -offset;
    } else {
        price += aggressive ? -offset : offset;
    }

    return std::max(1, price);
}

} // namespace

void validateGeneratorConfig(const GeneratorConfig& config) {
    if (config.events == 0) {
        throw std::invalid_argument("Generator events must be positive");
    }

    if (config.output_path.empty()) {
        throw std::invalid_argument("Generator output path cannot be empty");
    }

    if (config.cancel_probability < 0.0 || config.cancel_probability > 1.0) {
        throw std::invalid_argument("cancel_probability must be between 0 and 1");
    }

    if (config.market_probability < 0.0 || config.market_probability > 1.0) {
        throw std::invalid_argument("market_probability must be between 0 and 1");
    }

    if (config.volatility < 0) {
        throw std::invalid_argument("volatility must be non-negative");
    }

    if (config.average_order_size <= 0) {
        throw std::invalid_argument("average_order_size must be positive");
    }

    if (config.starting_price <= 0) {
        throw std::invalid_argument("starting_price must be positive");
    }
}

void generateSyntheticCsv(const GeneratorConfig& config) {
    validateGeneratorConfig(config);

    const std::filesystem::path output_path(config.output_path);
    if (!output_path.parent_path().empty()) {
        std::filesystem::create_directories(output_path.parent_path());
    }

    std::ofstream output(config.output_path);
    if (!output.is_open()) {
        throw std::runtime_error("Failed to open generator output file: " + config.output_path);
    }

    output << "timestamp,action,order_id,side,type,price,quantity\n";

    std::mt19937_64 rng(config.seed);
    std::bernoulli_distribution cancel_dist(config.cancel_probability);
    std::bernoulli_distribution market_dist(config.market_probability);
    std::bernoulli_distribution side_dist(0.5);

    OrderBook book;
    std::unordered_map<uint64_t, LiveOrderIndex> live_orders;
    std::vector<uint64_t> live_ids;
    uint64_t next_order_id = 1;
    int reference_price = config.starting_price;

    for (size_t event_number = 1; event_number <= config.events; ++event_number) {
        const uint64_t timestamp = static_cast<uint64_t>(event_number);
        const bool should_cancel = !live_ids.empty() && cancel_dist(rng);

        if (should_cancel) {
            std::uniform_int_distribution<size_t> index_dist(0, live_ids.size() - 1);
            const uint64_t order_id = live_ids[index_dist(rng)];
            const Order order = live_orders.at(order_id).order;

            output << timestamp
                   << ",CANCEL,"
                   << order.id
                   << ','
                   << sideToString(order.side)
                   << ','
                   << orderTypeToString(order.type)
                   << ','
                   << order.price
                   << ','
                   << order.quantity
                   << '\n';

            book.cancelOrder(order_id);
            removeLiveOrder(live_orders, live_ids, order_id);
            continue;
        }

        reference_price = nextReferencePrice(rng, reference_price, config.volatility);

        const Side side = side_dist(rng) ? Side::Buy : Side::Sell;
        const OrderType type = market_dist(rng) ? OrderType::Market : OrderType::Limit;
        const int quantity = nextPositiveQuantity(rng, config.average_order_size);
        const int price = type == OrderType::Market
            ? 0
            : nextLimitPrice(rng, reference_price, config.volatility, side);
        const uint64_t order_id = next_order_id++;

        output << timestamp
               << ",ADD,"
               << order_id
               << ','
               << sideToString(side)
               << ','
               << orderTypeToString(type)
               << ','
               << price
               << ','
               << quantity
               << '\n';

        Order order{order_id, side, type, price, quantity, timestamp};
        std::vector<Trade> trades = book.addOrder(order);
        int remaining_quantity = quantity;

        for (const Trade& trade : trades) {
            remaining_quantity -= trade.quantity;
            const uint64_t resting_id = side == Side::Buy ? trade.sell_order_id : trade.buy_order_id;
            auto live_it = live_orders.find(resting_id);
            if (live_it == live_orders.end()) {
                continue;
            }

            live_it->second.order.quantity -= trade.quantity;
            if (live_it->second.order.quantity == 0) {
                removeLiveOrder(live_orders, live_ids, resting_id);
            }
        }

        if (type == OrderType::Limit && remaining_quantity > 0) {
            order.quantity = remaining_quantity;
            const size_t index = live_ids.size();
            live_ids.push_back(order.id);
            live_orders.emplace(order.id, LiveOrderIndex{order, index});
        }
    }
}
