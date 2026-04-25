#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "Benchmark.hpp"
#include "CsvParser.hpp"
#include "Order.hpp"
#include "OrderBook.hpp"
#include "Replay.hpp"
#include "SyntheticOrderGenerator.hpp"

namespace {

void expect(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

Order makeOrder(
    uint64_t id,
    Side side,
    OrderType type,
    int price,
    int quantity,
    uint64_t timestamp = 1
) {
    return Order{id, side, type, price, quantity, timestamp};
}

void assertTrade(
    const Trade& trade,
    uint64_t buy_order_id,
    uint64_t sell_order_id,
    int price,
    int quantity,
    uint64_t timestamp,
    const std::string& context
) {
    expect(trade.buy_order_id == buy_order_id, context + ": unexpected buy_order_id");
    expect(trade.sell_order_id == sell_order_id, context + ": unexpected sell_order_id");
    expect(trade.price == price, context + ": unexpected price");
    expect(trade.quantity == quantity, context + ": unexpected quantity");
    expect(trade.timestamp == timestamp, context + ": unexpected timestamp");
}

void testAddLimitOrder() {
    OrderBook book;
    const std::vector<Trade> trades = book.addOrder(makeOrder(1, Side::Buy, OrderType::Limit, 5000, 100));

    expect(trades.empty(), "Limit add should not trade");
    expect(book.getBestBid() == 5000, "Best bid should be 50.00");
    expect(book.getBestAsk() == -1, "Best ask should be empty");
    expect(book.activeOrderCount() == 1, "One resting order expected");
}

void testBasicMatch() {
    OrderBook book;
    book.addOrder(makeOrder(1, Side::Sell, OrderType::Limit, 5000, 100, 1));
    const std::vector<Trade> trades = book.addOrder(makeOrder(2, Side::Buy, OrderType::Limit, 5000, 100, 2));

    expect(trades.size() == 1, "Expected one trade");
    assertTrade(trades.front(), 2, 1, 5000, 100, 2, "Basic match");
    expect(book.getBestBid() == -1 && book.getBestAsk() == -1, "Book should be empty after full match");
    expect(book.activeOrderCount() == 0, "No active orders expected");
}

void testPartialFill() {
    OrderBook book;
    book.addOrder(makeOrder(1, Side::Sell, OrderType::Limit, 5000, 100, 1));
    const std::vector<Trade> trades = book.addOrder(makeOrder(2, Side::Buy, OrderType::Limit, 5000, 40, 2));

    expect(trades.size() == 1, "Expected partial fill trade");
    assertTrade(trades.front(), 2, 1, 5000, 40, 2, "Partial fill");
    expect(book.getBestAsk() == 5000, "Remaining ask should stay at 50.00");
    expect(book.getAskDepth(5000) == 60, "Remaining ask quantity should be 60");
    expect(book.activeOrderCount() == 1, "One resting sell should remain");
}

void testPriceTimePriority() {
    OrderBook book;
    book.addOrder(makeOrder(1, Side::Sell, OrderType::Limit, 5000, 100, 1));
    book.addOrder(makeOrder(2, Side::Sell, OrderType::Limit, 5000, 100, 2));
    const std::vector<Trade> trades = book.addOrder(makeOrder(3, Side::Buy, OrderType::Limit, 5000, 150, 3));

    expect(trades.size() == 2, "Expected two trades for FIFO price level");
    assertTrade(trades[0], 3, 1, 5000, 100, 3, "Price-time priority trade 1");
    assertTrade(trades[1], 3, 2, 5000, 50, 3, "Price-time priority trade 2");
    expect(book.getAskDepth(5000) == 50, "Second sell order should have 50 remaining");
    expect(book.activeOrderCount() == 1, "One active order should remain");
}

void testBetterPriceFirst() {
    OrderBook book;
    book.addOrder(makeOrder(1, Side::Sell, OrderType::Limit, 5010, 100, 1));
    book.addOrder(makeOrder(2, Side::Sell, OrderType::Limit, 5000, 100, 2));
    const std::vector<Trade> trades = book.addOrder(makeOrder(3, Side::Buy, OrderType::Limit, 5010, 100, 3));

    expect(trades.size() == 1, "Expected one trade for best-price test");
    assertTrade(trades.front(), 3, 2, 5000, 100, 3, "Better price priority");
    expect(book.getBestAsk() == 5010, "Higher priced ask should remain");
}

void testCancelOrder() {
    OrderBook book;
    book.addOrder(makeOrder(1, Side::Buy, OrderType::Limit, 5000, 100));
    expect(book.cancelOrder(1), "Cancel should succeed");
    expect(book.activeOrderCount() == 0, "Book should be empty after cancel");
    expect(book.getBestBid() == -1, "Best bid should be empty after cancel");
}

void testCancelMissingOrder() {
    OrderBook book;
    expect(!book.cancelOrder(999), "Cancel missing should return false");
}

void testMarketOrderSweep() {
    OrderBook book;
    book.addOrder(makeOrder(1, Side::Sell, OrderType::Limit, 5000, 100, 1));
    book.addOrder(makeOrder(2, Side::Sell, OrderType::Limit, 5010, 100, 2));
    const std::vector<Trade> trades = book.addOrder(makeOrder(3, Side::Buy, OrderType::Market, 0, 150, 3));

    expect(trades.size() == 2, "Market buy should sweep two price levels");
    assertTrade(trades[0], 3, 1, 5000, 100, 3, "Market sweep 1");
    assertTrade(trades[1], 3, 2, 5010, 50, 3, "Market sweep 2");
    expect(book.getAskDepth(5010) == 50, "Remaining ask should be 50 at 50.10");
    expect(book.activeOrderCount() == 1, "One ask should remain");
}

void testMarketOrderOnEmptyBook() {
    OrderBook book;
    const std::vector<Trade> trades = book.addOrder(makeOrder(1, Side::Buy, OrderType::Market, 0, 50));

    expect(trades.empty(), "Market order on empty book should not trade");
    expect(book.activeOrderCount() == 0, "Market order should never rest");
}

void testCleanupAndDepth() {
    OrderBook book;
    book.addOrder(makeOrder(1, Side::Buy, OrderType::Limit, 5000, 40, 1));
    book.addOrder(makeOrder(2, Side::Buy, OrderType::Limit, 5000, 60, 2));
    book.addOrder(makeOrder(3, Side::Sell, OrderType::Limit, 5000, 100, 3));

    expect(book.getBidDepth(5000) == 0, "Filled price level should be erased");
    expect(book.getBestBid() == -1, "No bids should remain after cleanup");
    expect(book.activeOrderCount() == 0, "All orders should be filled");
}

void testDuplicateActiveIdRejection() {
    OrderBook book;
    book.addOrder(makeOrder(1, Side::Buy, OrderType::Limit, 5000, 100, 1));

    bool threw = false;
    try {
        book.addOrder(makeOrder(1, Side::Sell, OrderType::Limit, 5000, 50, 2));
    } catch (const std::invalid_argument&) {
        threw = true;
    }

    expect(threw, "Duplicate active order id should throw");
}

void testTopKDepth() {
    OrderBook book;
    book.addOrder(makeOrder(1, Side::Buy, OrderType::Limit, 10000, 50, 1));
    book.addOrder(makeOrder(2, Side::Buy, OrderType::Limit, 9950, 30, 2));
    book.addOrder(makeOrder(3, Side::Buy, OrderType::Limit, 9900, 20, 3));
    book.addOrder(makeOrder(4, Side::Sell, OrderType::Limit, 10050, 10, 4));
    book.addOrder(makeOrder(5, Side::Sell, OrderType::Limit, 10100, 20, 5));
    book.addOrder(makeOrder(6, Side::Sell, OrderType::Limit, 10150, 40, 6));

    const auto top_bids = book.getTopBids(2);
    const auto top_asks = book.getTopAsks(2);

    expect(top_bids.size() == 2, "Expected two top bids");
    expect(top_bids[0] == std::make_pair(10000, 50), "Top bid level mismatch");
    expect(top_bids[1] == std::make_pair(9950, 30), "Second bid level mismatch");

    expect(top_asks.size() == 2, "Expected two top asks");
    expect(top_asks[0] == std::make_pair(10050, 10), "Top ask level mismatch");
    expect(top_asks[1] == std::make_pair(10100, 20), "Second ask level mismatch");
}

void testReplayIntegration() {
    const std::vector<ReplayEvent> events = parseCsvFile("data/sample_orders.csv");
    OrderBook book;
    const ReplayResult result = replayEvents(events, book, true);

    expect(result.processed_events == 8, "Sample replay should process 8 events");
    expect(result.generated_trades == 3, "Sample replay should generate 3 trades");
    expect(result.active_orders == 3, "Sample replay should end with 3 active orders");
    expect(result.best_bid == 10065, "Final best bid should be 100.65");
    expect(result.best_ask == 10070, "Final best ask should be 100.70");
    expect(result.latencies_ns.size() == 8, "Benchmark replay should record every event latency");
    expect(result.trades.size() == 3, "Trade export should contain 3 trades");

    assertTrade(result.trades[0], 1004, 1003, 10060, 120, 4, "Replay trade 1");
    assertTrade(result.trades[1], 1004, 1005, 10065, 20, 6, "Replay trade 2");
    assertTrade(result.trades[2], 1006, 1002, 10070, 70, 7, "Replay trade 3");

    const std::filesystem::path temp_path = std::filesystem::temp_directory_path() / "lob_test_trades.csv";
    writeTradesCsv(temp_path.string(), result.trades);

    std::ifstream input(temp_path);
    expect(input.good(), "Trade CSV should be readable");

    std::vector<std::string> lines;
    std::string line;
    while (std::getline(input, line)) {
        lines.push_back(line);
    }

    expect(lines.size() == 4, "Trade CSV should contain header plus 3 rows");
    expect(lines[0] == "timestamp,buy_order_id,sell_order_id,price,quantity", "Trade CSV header mismatch");
    expect(lines[1] == "4,1004,1003,10060,120", "First trade CSV row mismatch");
}

void testGeneratorOutputParseability() {
    const std::filesystem::path temp_path = std::filesystem::temp_directory_path() / "lob_generated_test.csv";
    const GeneratorConfig config{
        120,
        temp_path.string(),
        0.20,
        0.10,
        15,
        40,
        123456,
        10000,
    };

    generateSyntheticCsv(config);

    const std::vector<ReplayEvent> events = parseCsvFile(temp_path.string());
    expect(events.size() == 120, "Generator should emit requested number of events");

    OrderBook book;
    const ReplayResult result = replayEvents(events, book, false);
    expect(result.processed_events == 120, "Generated file should replay cleanly");
}

void testVisualizationTraceExport() {
    const std::vector<ReplayEvent> events = parseCsvFile("data/sample_orders.csv");
    OrderBook book;

    ReplayOptions options;
    options.visualization_depth = 2;
    options.visualization_frame_step = 3;

    const ReplayResult result = replayEvents(events, book, options);
    expect(result.visualization.depth == 2, "Visualization depth should be recorded");
    expect(result.visualization.frame_step == 3, "Visualization frame step should be recorded");
    expect(result.visualization.frames.size() == 3, "Expected sampled frames at events 3, 6, and 8");

    const VisualizationFrame& first_frame = result.visualization.frames[0];
    expect(first_frame.event_index == 2, "First sampled frame should be event index 2");
    expect(first_frame.best_bid == 10050, "First sampled frame best bid mismatch");
    expect(first_frame.best_ask == 10060, "First sampled frame best ask mismatch");
    expect(first_frame.asks.size() == 2, "Expected two ask levels in first sampled frame");

    const VisualizationFrame& second_frame = result.visualization.frames[1];
    expect(second_frame.event_index == 5, "Second sampled frame should be event index 5");
    expect(second_frame.trades.size() == 1, "Second sampled frame should include one trade");
    expect(second_frame.matched_quantity == 20, "Second sampled frame matched quantity mismatch");
    expect(second_frame.active_orders == 2, "Second sampled frame active order count mismatch");

    const VisualizationFrame& final_frame = result.visualization.frames[2];
    expect(final_frame.event_index == 7, "Final sampled frame should be the last event");
    expect(final_frame.best_bid == 10065, "Final sampled frame best bid mismatch");
    expect(final_frame.best_ask == 10070, "Final sampled frame best ask mismatch");
    expect(final_frame.cumulative_traded_quantity == 210, "Cumulative traded quantity mismatch");

    const std::filesystem::path temp_path = std::filesystem::temp_directory_path() / "lob_visualization_trace.json";
    writeVisualizationJson(temp_path.string(), "sample_orders.csv", result);

    std::ifstream input(temp_path);
    expect(input.good(), "Visualization trace JSON should be readable");

    const std::string contents((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    expect(contents.find("\"schemaVersion\": 1") != std::string::npos, "Visualization JSON schema version missing");
    expect(contents.find("\"sourceName\": \"sample_orders.csv\"") != std::string::npos, "Visualization source missing");
    expect(contents.find("\"capturedFrames\": 3") != std::string::npos, "Visualization captured frame count missing");
    expect(contents.find("\"eventIndex\": 7") != std::string::npos, "Visualization final frame index missing");
}

void runTest(const std::string& name, void (*test_fn)()) {
    test_fn();
    std::cout << "[PASS] " << name << '\n';
}

} // namespace

int main() {
    try {
        runTest("AddLimitOrder", testAddLimitOrder);
        runTest("BasicMatch", testBasicMatch);
        runTest("PartialFill", testPartialFill);
        runTest("PriceTimePriority", testPriceTimePriority);
        runTest("BetterPriceFirst", testBetterPriceFirst);
        runTest("CancelOrder", testCancelOrder);
        runTest("CancelMissingOrder", testCancelMissingOrder);
        runTest("MarketOrderSweep", testMarketOrderSweep);
        runTest("MarketOrderOnEmptyBook", testMarketOrderOnEmptyBook);
        runTest("CleanupAndDepth", testCleanupAndDepth);
        runTest("DuplicateActiveIdRejection", testDuplicateActiveIdRejection);
        runTest("TopKDepth", testTopKDepth);
        runTest("ReplayIntegration", testReplayIntegration);
        runTest("GeneratorOutputParseability", testGeneratorOutputParseability);
        runTest("VisualizationTraceExport", testVisualizationTraceExport);
    } catch (const std::exception& exception) {
        std::cerr << "[FAIL] " << exception.what() << '\n';
        return 1;
    }

    std::cout << "All tests passed.\n";
    return 0;
}
