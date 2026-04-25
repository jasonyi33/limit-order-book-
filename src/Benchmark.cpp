#include "Benchmark.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>

#include "Replay.hpp"

namespace {

double computePercentile(const std::vector<uint64_t>& sorted_values, double percentile) {
    if (sorted_values.empty()) {
        return 0.0;
    }

    const double index = percentile * static_cast<double>(sorted_values.size() - 1);
    const size_t lower = static_cast<size_t>(index);
    const size_t upper = std::min(lower + 1, sorted_values.size() - 1);
    const double weight = index - static_cast<double>(lower);

    return static_cast<double>(sorted_values[lower]) * (1.0 - weight) +
           static_cast<double>(sorted_values[upper]) * weight;
}

std::string jsonEscape(const std::string& value) {
    std::ostringstream stream;
    for (char ch : value) {
        switch (ch) {
        case '\\':
            stream << "\\\\";
            break;
        case '"':
            stream << "\\\"";
            break;
        case '\n':
            stream << "\\n";
            break;
        case '\r':
            stream << "\\r";
            break;
        case '\t':
            stream << "\\t";
            break;
        default:
            stream << ch;
            break;
        }
    }
    return stream.str();
}

std::vector<DepthLevel> toDepthLevels(const std::vector<std::pair<int, int>>& levels) {
    std::vector<DepthLevel> depth_levels;
    depth_levels.reserve(levels.size());
    for (const auto& [price, quantity] : levels) {
        depth_levels.push_back(DepthLevel{price, quantity});
    }
    return depth_levels;
}

bool shouldCaptureFrame(size_t event_index, size_t event_count, int frame_step) {
    if (frame_step <= 1) {
        return true;
    }

    return ((event_index + 1) % static_cast<size_t>(frame_step) == 0) || (event_index + 1 == event_count);
}

void writeJsonOrder(std::ostream& output, const Order& order) {
    output << '{'
           << "\"orderId\":" << order.id << ','
           << "\"side\":\"" << sideToString(order.side) << "\","
           << "\"type\":\"" << orderTypeToString(order.type) << "\","
           << "\"price\":" << order.price << ','
           << "\"quantity\":" << order.quantity << ','
           << "\"timestamp\":" << order.timestamp
           << '}';
}

void writeJsonTrade(std::ostream& output, const Trade& trade) {
    output << '{'
           << "\"buyOrderId\":" << trade.buy_order_id << ','
           << "\"sellOrderId\":" << trade.sell_order_id << ','
           << "\"price\":" << trade.price << ','
           << "\"quantity\":" << trade.quantity << ','
           << "\"timestamp\":" << trade.timestamp
           << '}';
}

void writeJsonDepthLevel(std::ostream& output, const DepthLevel& level) {
    output << '{'
           << "\"price\":" << level.price << ','
           << "\"quantity\":" << level.quantity
           << '}';
}

} // namespace

ReplayResult replayEvents(const std::vector<ReplayEvent>& events, OrderBook& book, const ReplayOptions& options) {
    ReplayResult result;
    result.processed_events = events.size();
    result.visualization.depth = std::max(0, options.visualization_depth);
    result.visualization.frame_step = std::max(1, options.visualization_frame_step);

    const bool capture_event_metrics = options.benchmark || result.visualization.depth > 0;
    if (capture_event_metrics) {
        result.latencies_ns.reserve(events.size());
    }
    if (result.visualization.depth > 0) {
        const size_t estimated_frames =
            (events.size() + static_cast<size_t>(result.visualization.frame_step) - 1) /
            static_cast<size_t>(result.visualization.frame_step);
        result.visualization.frames.reserve(estimated_frames == 0 ? 1 : estimated_frames);
    }

    const auto replay_start = std::chrono::high_resolution_clock::now();
    uint64_t cumulative_traded_quantity = 0;
    size_t cumulative_trade_count = 0;

    for (size_t event_index = 0; event_index < events.size(); ++event_index) {
        const ReplayEvent& event = events[event_index];
        const auto event_start = std::chrono::high_resolution_clock::now();
        std::vector<Trade> trades;
        bool cancel_succeeded = false;

        if (event.action == EventAction::Add) {
            trades = book.addOrder(event.order);
            result.generated_trades += trades.size();
            result.trades.insert(result.trades.end(), trades.begin(), trades.end());
        } else {
            cancel_succeeded = book.cancelOrder(event.order.id);
        }

        uint64_t event_latency = 0;
        if (capture_event_metrics) {
            const auto event_end = std::chrono::high_resolution_clock::now();
            event_latency = static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(event_end - event_start).count());
            result.latencies_ns.push_back(static_cast<uint64_t>(event_latency));
        }

        int matched_quantity = 0;
        for (const Trade& trade : trades) {
            matched_quantity += trade.quantity;
            cumulative_traded_quantity += static_cast<uint64_t>(trade.quantity);
        }
        cumulative_trade_count += trades.size();

        if (result.visualization.depth > 0 && shouldCaptureFrame(event_index, events.size(), result.visualization.frame_step)) {
            VisualizationFrame frame;
            frame.event_index = event_index;
            frame.event = event;
            frame.cancel_succeeded = cancel_succeeded;
            frame.matched_quantity = matched_quantity;
            frame.remaining_quantity = event.action == EventAction::Add
                ? std::max(0, event.order.quantity - matched_quantity)
                : 0;
            frame.latency_ns = event_latency;
            frame.active_orders = book.activeOrderCount();
            frame.best_bid = book.getBestBid();
            frame.best_ask = book.getBestAsk();
            frame.spread = (frame.best_bid >= 0 && frame.best_ask >= 0) ? (frame.best_ask - frame.best_bid) : -1;
            frame.cumulative_trade_count = cumulative_trade_count;
            frame.cumulative_traded_quantity = cumulative_traded_quantity;
            frame.bids = toDepthLevels(book.getTopBids(result.visualization.depth));
            frame.asks = toDepthLevels(book.getTopAsks(result.visualization.depth));
            frame.trades = trades;
            result.visualization.frames.push_back(std::move(frame));
        }
    }
    const auto replay_end = std::chrono::high_resolution_clock::now();

    result.total_runtime_ns =
        static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(replay_end - replay_start).count());
    result.active_orders = book.activeOrderCount();
    result.best_bid = book.getBestBid();
    result.best_ask = book.getBestAsk();
    return result;
}

ReplayResult replayEvents(const std::vector<ReplayEvent>& events, OrderBook& book, bool benchmark) {
    ReplayOptions options;
    options.benchmark = benchmark;
    return replayEvents(events, book, options);
}

void writeTradesCsv(const std::string& path, const std::vector<Trade>& trades) {
    const std::filesystem::path output_path(path);
    if (!output_path.parent_path().empty()) {
        std::filesystem::create_directories(output_path.parent_path());
    }

    std::ofstream output(path);
    if (!output.is_open()) {
        throw std::runtime_error("Failed to open trade output file: " + path);
    }

    output << "timestamp,buy_order_id,sell_order_id,price,quantity\n";
    for (const Trade& trade : trades) {
        output << trade.timestamp << ','
               << trade.buy_order_id << ','
               << trade.sell_order_id << ','
               << trade.price << ','
               << trade.quantity << '\n';
    }
}

void writeVisualizationJson(const std::string& path, const std::string& source_name, const ReplayResult& result) {
    const std::filesystem::path output_path(path);
    if (!output_path.parent_path().empty()) {
        std::filesystem::create_directories(output_path.parent_path());
    }

    std::ofstream output(path);
    if (!output.is_open()) {
        throw std::runtime_error("Failed to open visualization output file: " + path);
    }

    const LatencyStats stats = computeLatencyStats(result.latencies_ns);
    const int spread = (result.best_bid >= 0 && result.best_ask >= 0) ? (result.best_ask - result.best_bid) : -1;

    output << "{\n";
    output << "  \"schemaVersion\": 1,\n";
    output << "  \"sourceName\": \"" << jsonEscape(source_name) << "\",\n";
    output << "  \"summary\": {\n";
    output << "    \"processedEvents\": " << result.processed_events << ",\n";
    output << "    \"generatedTrades\": " << result.generated_trades << ",\n";
    output << "    \"activeOrders\": " << result.active_orders << ",\n";
    output << "    \"bestBid\": " << result.best_bid << ",\n";
    output << "    \"bestAsk\": " << result.best_ask << ",\n";
    output << "    \"spread\": " << spread << ",\n";
    output << "    \"totalRuntimeNs\": " << result.total_runtime_ns << ",\n";
    output << "    \"latencyStats\": {\n";
    output << "      \"medianNs\": " << stats.median_ns << ",\n";
    output << "      \"p95Ns\": " << stats.p95_ns << ",\n";
    output << "      \"p99Ns\": " << stats.p99_ns << ",\n";
    output << "      \"maxNs\": " << stats.max_ns << "\n";
    output << "    }\n";
    output << "  },\n";
    output << "  \"visualization\": {\n";
    output << "    \"depth\": " << result.visualization.depth << ",\n";
    output << "    \"frameStep\": " << result.visualization.frame_step << ",\n";
    output << "    \"capturedFrames\": " << result.visualization.frames.size() << "\n";
    output << "  },\n";
    output << "  \"frames\": [\n";

    for (size_t index = 0; index < result.visualization.frames.size(); ++index) {
        const VisualizationFrame& frame = result.visualization.frames[index];
        output << "    {\n";
        output << "      \"eventIndex\": " << frame.event_index << ",\n";
        output << "      \"timestamp\": " << frame.event.timestamp << ",\n";
        output << "      \"action\": \"" << (frame.event.action == EventAction::Add ? "ADD" : "CANCEL") << "\",\n";
        output << "      \"cancelSucceeded\": " << (frame.cancel_succeeded ? "true" : "false") << ",\n";
        output << "      \"matchedQuantity\": " << frame.matched_quantity << ",\n";
        output << "      \"remainingQuantity\": " << frame.remaining_quantity << ",\n";
        output << "      \"latencyNs\": " << frame.latency_ns << ",\n";
        output << "      \"activeOrders\": " << frame.active_orders << ",\n";
        output << "      \"bestBid\": " << frame.best_bid << ",\n";
        output << "      \"bestAsk\": " << frame.best_ask << ",\n";
        output << "      \"spread\": " << frame.spread << ",\n";
        output << "      \"cumulativeTradeCount\": " << frame.cumulative_trade_count << ",\n";
        output << "      \"cumulativeTradedQuantity\": " << frame.cumulative_traded_quantity << ",\n";
        output << "      \"event\": ";
        writeJsonOrder(output, frame.event.order);
        output << ",\n";

        output << "      \"bids\": [";
        for (size_t level_index = 0; level_index < frame.bids.size(); ++level_index) {
            if (level_index > 0) {
                output << ',';
            }
            writeJsonDepthLevel(output, frame.bids[level_index]);
        }
        output << "],\n";

        output << "      \"asks\": [";
        for (size_t level_index = 0; level_index < frame.asks.size(); ++level_index) {
            if (level_index > 0) {
                output << ',';
            }
            writeJsonDepthLevel(output, frame.asks[level_index]);
        }
        output << "],\n";

        output << "      \"trades\": [";
        for (size_t trade_index = 0; trade_index < frame.trades.size(); ++trade_index) {
            if (trade_index > 0) {
                output << ',';
            }
            writeJsonTrade(output, frame.trades[trade_index]);
        }
        output << "]\n";
        output << "    }";
        if (index + 1 != result.visualization.frames.size()) {
            output << ',';
        }
        output << '\n';
    }

    output << "  ]\n";
    output << "}\n";
}

LatencyStats computeLatencyStats(const std::vector<uint64_t>& latencies_ns) {
    if (latencies_ns.empty()) {
        return {};
    }

    std::vector<uint64_t> sorted_latencies = latencies_ns;
    std::sort(sorted_latencies.begin(), sorted_latencies.end());

    LatencyStats stats;
    stats.median_ns = computePercentile(sorted_latencies, 0.50);
    stats.p95_ns = computePercentile(sorted_latencies, 0.95);
    stats.p99_ns = computePercentile(sorted_latencies, 0.99);
    stats.max_ns = static_cast<double>(sorted_latencies.back());
    return stats;
}

double computeThroughput(size_t event_count, uint64_t total_runtime_ns) {
    if (event_count == 0 || total_runtime_ns == 0) {
        return 0.0;
    }

    return static_cast<double>(event_count) / (static_cast<double>(total_runtime_ns) / 1'000'000'000.0);
}

std::string formatNumberWithCommas(uint64_t value) {
    std::string digits = std::to_string(value);
    for (int insert_at = static_cast<int>(digits.size()) - 3; insert_at > 0; insert_at -= 3) {
        digits.insert(static_cast<size_t>(insert_at), ",");
    }
    return digits;
}

std::string formatMicros(double nanoseconds) {
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(2) << nanoseconds / 1000.0 << " us";
    return stream.str();
}

std::string formatMilliseconds(uint64_t nanoseconds) {
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(3) << static_cast<double>(nanoseconds) / 1'000'000.0 << " ms";
    return stream.str();
}
