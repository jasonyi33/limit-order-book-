#include "OrderBook.hpp"

#include <algorithm>
#include <stdexcept>

std::vector<Trade> OrderBook::addOrder(const Order& order) {
    validateOrder(order);

    Order incoming = order;
    std::vector<Trade> trades;

    if (incoming.side == Side::Buy) {
        matchBuyOrder(incoming, trades);
    } else {
        matchSellOrder(incoming, trades);
    }

    if (incoming.quantity > 0 && incoming.type == OrderType::Limit) {
        restOrder(incoming);
    }

    return trades;
}

bool OrderBook::cancelOrder(uint64_t order_id) {
    auto lookup_it = order_lookup_.find(order_id);
    if (lookup_it == order_lookup_.end()) {
        return false;
    }

    const OrderLocation location = lookup_it->second;
    if (location.side == Side::Buy) {
        auto level_it = bids_.find(location.price);
        if (level_it == bids_.end()) {
            return false;
        }

        level_it->second.total_quantity -= location.it->quantity;
        level_it->second.orders.erase(location.it);
        if (level_it->second.orders.empty()) {
            bids_.erase(level_it);
        }
    } else {
        auto level_it = asks_.find(location.price);
        if (level_it == asks_.end()) {
            return false;
        }

        level_it->second.total_quantity -= location.it->quantity;
        level_it->second.orders.erase(location.it);
        if (level_it->second.orders.empty()) {
            asks_.erase(level_it);
        }
    }

    order_lookup_.erase(lookup_it);
    return true;
}

int OrderBook::getBestBid() const {
    return bids_.empty() ? -1 : bids_.begin()->first;
}

int OrderBook::getBestAsk() const {
    return asks_.empty() ? -1 : asks_.begin()->first;
}

int OrderBook::getBidDepth(int price) const {
    auto it = bids_.find(price);
    return it == bids_.end() ? 0 : it->second.total_quantity;
}

int OrderBook::getAskDepth(int price) const {
    auto it = asks_.find(price);
    return it == asks_.end() ? 0 : it->second.total_quantity;
}

size_t OrderBook::activeOrderCount() const {
    return order_lookup_.size();
}

std::vector<std::pair<int, int>> OrderBook::getTopBids(int k) const {
    std::vector<std::pair<int, int>> levels;
    if (k <= 0) {
        return levels;
    }

    levels.reserve(static_cast<size_t>(k));
    for (auto it = bids_.begin(); it != bids_.end() && static_cast<int>(levels.size()) < k; ++it) {
        levels.emplace_back(it->first, it->second.total_quantity);
    }

    return levels;
}

std::vector<std::pair<int, int>> OrderBook::getTopAsks(int k) const {
    std::vector<std::pair<int, int>> levels;
    if (k <= 0) {
        return levels;
    }

    levels.reserve(static_cast<size_t>(k));
    for (auto it = asks_.begin(); it != asks_.end() && static_cast<int>(levels.size()) < k; ++it) {
        levels.emplace_back(it->first, it->second.total_quantity);
    }

    return levels;
}

void OrderBook::printBook() const {
    printBook(std::cout);
}

void OrderBook::printBook(std::ostream& stream) const {
    stream << "ASKS\n";
    for (auto it = asks_.rbegin(); it != asks_.rend(); ++it) {
        stream << formatPrice(it->first) << " | " << it->second.total_quantity << '\n';
    }

    stream << "\nBIDS\n";
    for (const auto& [price, level] : bids_) {
        stream << formatPrice(price) << " | " << level.total_quantity << '\n';
    }
}

void OrderBook::validateOrder(const Order& order) const {
    if (order.quantity <= 0) {
        throw std::invalid_argument("Order quantity must be positive");
    }

    if (order.type == OrderType::Limit && order.price < 0) {
        throw std::invalid_argument("Limit order price must be non-negative");
    }

    if (order.type == OrderType::Market && order.price != 0) {
        throw std::invalid_argument("Market order price must be 0");
    }

    if (order_lookup_.find(order.id) != order_lookup_.end()) {
        throw std::invalid_argument("Duplicate active order id");
    }
}

void OrderBook::matchBuyOrder(Order& incoming, std::vector<Trade>& trades) {
    while (incoming.quantity > 0 && !asks_.empty()) {
        auto level_it = asks_.begin();
        if (incoming.type == OrderType::Limit && level_it->first > incoming.price) {
            break;
        }

        PriceLevel& level = level_it->second;
        auto resting_it = level.orders.begin();
        Order& resting = *resting_it;
        const int matched_quantity = std::min(incoming.quantity, resting.quantity);

        trades.push_back(Trade{
            incoming.id,
            resting.id,
            level_it->first,
            matched_quantity,
            incoming.timestamp,
        });

        incoming.quantity -= matched_quantity;
        resting.quantity -= matched_quantity;
        level.total_quantity -= matched_quantity;

        if (resting.quantity == 0) {
            order_lookup_.erase(resting.id);
            level.orders.erase(resting_it);
        }

        if (level.orders.empty()) {
            asks_.erase(level_it);
        }
    }
}

void OrderBook::matchSellOrder(Order& incoming, std::vector<Trade>& trades) {
    while (incoming.quantity > 0 && !bids_.empty()) {
        auto level_it = bids_.begin();
        if (incoming.type == OrderType::Limit && level_it->first < incoming.price) {
            break;
        }

        PriceLevel& level = level_it->second;
        auto resting_it = level.orders.begin();
        Order& resting = *resting_it;
        const int matched_quantity = std::min(incoming.quantity, resting.quantity);

        trades.push_back(Trade{
            resting.id,
            incoming.id,
            level_it->first,
            matched_quantity,
            incoming.timestamp,
        });

        incoming.quantity -= matched_quantity;
        resting.quantity -= matched_quantity;
        level.total_quantity -= matched_quantity;

        if (resting.quantity == 0) {
            order_lookup_.erase(resting.id);
            level.orders.erase(resting_it);
        }

        if (level.orders.empty()) {
            bids_.erase(level_it);
        }
    }
}

void OrderBook::restOrder(const Order& order) {
    if (order.side == Side::Buy) {
        PriceLevel& level = bids_[order.price];
        level.orders.push_back(order);
        level.total_quantity += order.quantity;
        auto order_it = std::prev(level.orders.end());
        order_lookup_.emplace(order.id, OrderLocation{Side::Buy, order.price, order_it});
    } else {
        PriceLevel& level = asks_[order.price];
        level.orders.push_back(order);
        level.total_quantity += order.quantity;
        auto order_it = std::prev(level.orders.end());
        order_lookup_.emplace(order.id, OrderLocation{Side::Sell, order.price, order_it});
    }
}
