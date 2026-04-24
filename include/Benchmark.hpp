#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct LatencyStats {
    double median_ns = 0.0;
    double p95_ns = 0.0;
    double p99_ns = 0.0;
    double max_ns = 0.0;
};

LatencyStats computeLatencyStats(const std::vector<uint64_t>& latencies_ns);
double computeThroughput(size_t event_count, uint64_t total_runtime_ns);
std::string formatNumberWithCommas(uint64_t value);
std::string formatMicros(double nanoseconds);
std::string formatMilliseconds(uint64_t nanoseconds);
