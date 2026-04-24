#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

#include "SyntheticOrderGenerator.hpp"

namespace {

void printUsage() {
    std::cout
        << "Usage: ./generator [--events N] [--output path] [--cancel-prob p] [--market-prob p]\n"
        << "                   [--volatility cents] [--avg-size quantity] [--seed value]\n";
}

GeneratorConfig parseArgs(int argc, char** argv) {
    GeneratorConfig config;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--help") {
            printUsage();
            std::exit(0);
        }

        if (i + 1 >= argc) {
            throw std::invalid_argument("Missing value for argument: " + arg);
        }

        const std::string value = argv[++i];
        if (arg == "--events") {
            config.events = static_cast<size_t>(std::stoull(value));
        } else if (arg == "--output") {
            config.output_path = value;
        } else if (arg == "--cancel-prob") {
            config.cancel_probability = std::stod(value);
        } else if (arg == "--market-prob") {
            config.market_probability = std::stod(value);
        } else if (arg == "--volatility") {
            config.volatility = std::stoi(value);
        } else if (arg == "--avg-size") {
            config.average_order_size = std::stoi(value);
        } else if (arg == "--seed") {
            config.seed = std::stoull(value);
        } else {
            throw std::invalid_argument("Unknown argument: " + arg);
        }
    }

    return config;
}

} // namespace

int main(int argc, char** argv) {
    try {
        const GeneratorConfig config = parseArgs(argc, argv);
        generateSyntheticCsv(config);

        std::cout << "Generated " << config.events << " events\n";
        std::cout << "Output: " << config.output_path << '\n';
        std::cout << "Seed: " << config.seed << '\n';
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "Error: " << exception.what() << '\n';
        return 1;
    }
}
