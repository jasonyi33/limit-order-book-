#pragma once

#include <cstdint>
#include <functional>
#include <iostream>
#include <list>
#include <map>
#include <unordered_map>
#include <utility>
#include <vector>

#include "Order.hpp"
#include "Trade.hpp"

class OrderBook {
public:
    std::vector<Trade> addOrder(const Order& order);
    bool cancelOrder(uint64_t order_id);

    int getBestBid() const;
    int getBestAsk() const;

    int getBidDepth(int price) const;
    int getAskDepth(int price) const;

    size_t activeOrderCount() const;

    std::vector<std::pair<int, int>> getTopBids(int k) const;
    std::vector<std::pair<int, int>> getTopAsks(int k) const;

    void printBook() const;
    void printBook(std::ostream& stream) const;

private:
    struct PriceLevel {
        std::list<Order> orders;
        int total_quantity = 0;
    };

    struct OrderLocation {
        Side side;
        int price;
        std::list<Order>::iterator it;
    };

    using BidBook = std::map<int, PriceLevel, std::greater<int>>;
    using AskBook = std::map<int, PriceLevel>;

    void validateOrder(const Order& order) const;
    void matchBuyOrder(Order& incoming, std::vector<Trade>& trades);
    void matchSellOrder(Order& incoming, std::vector<Trade>& trades);
    void restOrder(const Order& order);

    BidBook bids_;
    AskBook asks_;
    std::unordered_map<uint64_t, OrderLocation> order_lookup_;
};
