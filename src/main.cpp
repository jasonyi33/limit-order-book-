#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>

#include "Benchmark.hpp"
#include "CsvParser.hpp"
#include "Order.hpp"
#include "OrderBook.hpp"
#include "Replay.hpp"

namespace {

struct CliOptions {
    std::string input_path;
    std::string trades_output_path;
    std::string visualization_output_path;
    bool print_book = false;
    bool benchmark = false;
    int visualization_depth = 12;
    int visualization_frame_step = 1;
};

void printUsage() {
    std::cout
        << "Usage: ./lob --input <csv> [--print-book] [--benchmark] [--trades <csv>]\n"
        << "             [--visualize <json>] [--visualize-depth <k>] [--visualize-frame-step <n>]\n"
        << "Options:\n"
        << "  --input <csv>     Input CSV file to replay\n"
        << "  --print-book      Print the final book state after replay\n"
        << "  --benchmark       Record per-event latency metrics\n"
        << "  --trades <csv>    Write executed trades to a CSV file\n"
        << "  --visualize <json>\n"
        << "                    Export a visualization trace JSON file\n"
        << "  --visualize-depth <k>\n"
        << "                    Capture the top k price levels per side (default: 12)\n"
        << "  --visualize-frame-step <n>\n"
        << "                    Capture every nth event plus the final event (default: 1)\n"
        << "  --help            Show this help message\n";
}

CliOptions parseArgs(int argc, char** argv) {
    CliOptions options;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--help") {
            printUsage();
            std::exit(0);
        }

        if (arg == "--input") {
            if (i + 1 >= argc) {
                throw std::invalid_argument("--input requires a path");
            }
            options.input_path = argv[++i];
            continue;
        }

        if (arg == "--print-book") {
            options.print_book = true;
            continue;
        }

        if (arg == "--benchmark") {
            options.benchmark = true;
            continue;
        }

        if (arg == "--trades") {
            if (i + 1 >= argc) {
                throw std::invalid_argument("--trades requires a path");
            }
            options.trades_output_path = argv[++i];
            continue;
        }

        if (arg == "--visualize") {
            if (i + 1 >= argc) {
                throw std::invalid_argument("--visualize requires a path");
            }
            options.visualization_output_path = argv[++i];
            continue;
        }

        if (arg == "--visualize-depth") {
            if (i + 1 >= argc) {
                throw std::invalid_argument("--visualize-depth requires a value");
            }
            options.visualization_depth = std::stoi(argv[++i]);
            continue;
        }

        if (arg == "--visualize-frame-step") {
            if (i + 1 >= argc) {
                throw std::invalid_argument("--visualize-frame-step requires a value");
            }
            options.visualization_frame_step = std::stoi(argv[++i]);
            continue;
        }

        throw std::invalid_argument("Unknown argument: " + arg);
    }

    if (options.input_path.empty()) {
        throw std::invalid_argument("--input is required");
    }

    if (!options.visualization_output_path.empty()) {
        if (options.visualization_depth <= 0) {
            throw std::invalid_argument("--visualize-depth must be positive");
        }

        if (options.visualization_frame_step <= 0) {
            throw std::invalid_argument("--visualize-frame-step must be positive");
        }
    }

    return options;
}

std::string formatBestPrice(int price) {
    return price < 0 ? "N/A" : formatPrice(price);
}

} // namespace

int main(int argc, char** argv) {
    try {
        const CliOptions options = parseArgs(argc, argv);
        const std::vector<ReplayEvent> events = parseCsvFile(options.input_path);

        OrderBook book;
        ReplayOptions replay_options;
        replay_options.benchmark = options.benchmark;
        if (!options.visualization_output_path.empty()) {
            replay_options.visualization_depth = options.visualization_depth;
            replay_options.visualization_frame_step = options.visualization_frame_step;
        }

        const ReplayResult result = replayEvents(events, book, replay_options);

        std::cout << "Processed " << formatNumberWithCommas(result.processed_events) << " events\n";
        std::cout << "Generated " << formatNumberWithCommas(result.generated_trades) << " trades\n";
        std::cout << "Active orders: " << formatNumberWithCommas(result.active_orders) << '\n';
        std::cout << "Best bid: " << formatBestPrice(result.best_bid) << '\n';
        std::cout << "Best ask: " << formatBestPrice(result.best_ask) << '\n';
        std::cout << "Total runtime: " << formatMilliseconds(result.total_runtime_ns) << '\n';
        std::cout << "Throughput: " << std::fixed << std::setprecision(2)
                  << computeThroughput(result.processed_events, result.total_runtime_ns) << " events/sec\n";

        if (options.benchmark) {
            const LatencyStats stats = computeLatencyStats(result.latencies_ns);
            std::cout << "Median latency: " << formatMicros(stats.median_ns) << '\n';
            std::cout << "p95 latency: " << formatMicros(stats.p95_ns) << '\n';
            std::cout << "p99 latency: " << formatMicros(stats.p99_ns) << '\n';
            std::cout << "Max latency: " << formatMicros(stats.max_ns) << '\n';
        }

        if (!options.trades_output_path.empty()) {
            writeTradesCsv(options.trades_output_path, result.trades);
            std::cout << "Trades written to: " << options.trades_output_path << '\n';
        }

        if (!options.visualization_output_path.empty()) {
            writeVisualizationJson(
                options.visualization_output_path,
                std::filesystem::path(options.input_path).filename().string(),
                result
            );
            std::cout << "Visualization trace written to: " << options.visualization_output_path << '\n';
        }

        if (options.print_book) {
            std::cout << '\n';
            book.printBook(std::cout);
            std::cout << '\n';
        }

        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "Error: " << exception.what() << '\n';
        return 1;
    }
}
