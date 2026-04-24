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

} // namespace

ReplayResult replayEvents(const std::vector<ReplayEvent>& events, OrderBook& book, bool benchmark) {
    ReplayResult result;
    result.processed_events = events.size();
    if (benchmark) {
        result.latencies_ns.reserve(events.size());
    }

    const auto replay_start = std::chrono::high_resolution_clock::now();
    for (const ReplayEvent& event : events) {
        const auto event_start = std::chrono::high_resolution_clock::now();

        if (event.action == EventAction::Add) {
            std::vector<Trade> trades = book.addOrder(event.order);
            result.generated_trades += trades.size();
            result.trades.insert(result.trades.end(), trades.begin(), trades.end());
        } else {
            book.cancelOrder(event.order.id);
        }

        if (benchmark) {
            const auto event_end = std::chrono::high_resolution_clock::now();
            const auto event_latency =
                std::chrono::duration_cast<std::chrono::nanoseconds>(event_end - event_start).count();
            result.latencies_ns.push_back(static_cast<uint64_t>(event_latency));
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
