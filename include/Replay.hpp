#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "CsvParser.hpp"
#include "OrderBook.hpp"
#include "Trade.hpp"

struct ReplayResult {
    size_t processed_events = 0;
    size_t generated_trades = 0;
    size_t active_orders = 0;
    int best_bid = -1;
    int best_ask = -1;
    uint64_t total_runtime_ns = 0;
    std::vector<uint64_t> latencies_ns;
    std::vector<Trade> trades;
};

ReplayResult replayEvents(const std::vector<ReplayEvent>& events, OrderBook& book, bool benchmark);
void writeTradesCsv(const std::string& path, const std::vector<Trade>& trades);
