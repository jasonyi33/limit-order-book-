#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

struct GeneratorConfig {
    size_t events = 100000;
    std::string output_path = "data/synthetic_orders.csv";
    double cancel_probability = 0.10;
    double market_probability = 0.05;
    int volatility = 10;
    int average_order_size = 100;
    uint64_t seed = 42;
    int starting_price = 10000;
};

void validateGeneratorConfig(const GeneratorConfig& config);
void generateSyntheticCsv(const GeneratorConfig& config);
