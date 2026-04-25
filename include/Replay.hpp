#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "CsvParser.hpp"
#include "OrderBook.hpp"
#include "Trade.hpp"

struct ReplayOptions {
    bool benchmark = false;
    int visualization_depth = 0;
    int visualization_frame_step = 1;
};

struct DepthLevel {
    int price = 0;
    int quantity = 0;
};

struct VisualizationFrame {
    size_t event_index = 0;
    ReplayEvent event{};
    bool cancel_succeeded = false;
    int matched_quantity = 0;
    int remaining_quantity = 0;
    uint64_t latency_ns = 0;
    size_t active_orders = 0;
    int best_bid = -1;
    int best_ask = -1;
    int spread = -1;
    size_t cumulative_trade_count = 0;
    uint64_t cumulative_traded_quantity = 0;
    std::vector<DepthLevel> bids;
    std::vector<DepthLevel> asks;
    std::vector<Trade> trades;
};

struct VisualizationTrace {
    int depth = 0;
    int frame_step = 1;
    std::vector<VisualizationFrame> frames;
};

struct ReplayResult {
    size_t processed_events = 0;
    size_t generated_trades = 0;
    size_t active_orders = 0;
    int best_bid = -1;
    int best_ask = -1;
    uint64_t total_runtime_ns = 0;
    std::vector<uint64_t> latencies_ns;
    std::vector<Trade> trades;
    VisualizationTrace visualization;
};

ReplayResult replayEvents(const std::vector<ReplayEvent>& events, OrderBook& book, const ReplayOptions& options);
ReplayResult replayEvents(const std::vector<ReplayEvent>& events, OrderBook& book, bool benchmark);
void writeTradesCsv(const std::string& path, const std::vector<Trade>& trades);
void writeVisualizationJson(const std::string& path, const std::string& source_name, const ReplayResult& result);
