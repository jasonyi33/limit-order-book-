#include "CsvParser.hpp"

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

std::string trim(const std::string& value) {
    const auto start = value.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) {
        return "";
    }

    const auto end = value.find_last_not_of(" \t\r\n");
    return value.substr(start, end - start + 1);
}

[[noreturn]] void throwParseError(size_t line_number, const std::string& message) {
    throw std::runtime_error("CSV parse error on line " + std::to_string(line_number) + ": " + message);
}

uint64_t parseUnsigned(const std::string& raw, size_t line_number, const std::string& field_name) {
    const std::string value = trim(raw);
    if (value.empty()) {
        throwParseError(line_number, field_name + " is empty");
    }

    size_t processed = 0;
    unsigned long long parsed = 0;
    try {
        parsed = std::stoull(value, &processed);
    } catch (const std::exception&) {
        throwParseError(line_number, "Invalid unsigned integer for " + field_name + ": " + value);
    }

    if (processed != value.size()) {
        throwParseError(line_number, "Unexpected characters in " + field_name + ": " + value);
    }

    return static_cast<uint64_t>(parsed);
}

int parseSignedInt(const std::string& raw, size_t line_number, const std::string& field_name) {
    const std::string value = trim(raw);
    if (value.empty()) {
        throwParseError(line_number, field_name + " is empty");
    }

    size_t processed = 0;
    long parsed = 0;
    try {
        parsed = std::stol(value, &processed);
    } catch (const std::exception&) {
        throwParseError(line_number, "Invalid integer for " + field_name + ": " + value);
    }

    if (processed != value.size()) {
        throwParseError(line_number, "Unexpected characters in " + field_name + ": " + value);
    }

    return static_cast<int>(parsed);
}

EventAction parseAction(const std::string& raw, size_t line_number) {
    const std::string value = trim(raw);
    if (value == "ADD") {
        return EventAction::Add;
    }

    if (value == "CANCEL") {
        return EventAction::Cancel;
    }

    throwParseError(line_number, "Unknown action: " + value);
}

Side parseSide(const std::string& raw, size_t line_number) {
    const std::string value = trim(raw);
    if (value == "BUY") {
        return Side::Buy;
    }

    if (value == "SELL") {
        return Side::Sell;
    }

    throwParseError(line_number, "Unknown side: " + value);
}

OrderType parseOrderType(const std::string& raw, size_t line_number) {
    const std::string value = trim(raw);
    if (value == "LIMIT") {
        return OrderType::Limit;
    }

    if (value == "MARKET") {
        return OrderType::Market;
    }

    throwParseError(line_number, "Unknown order type: " + value);
}

std::vector<std::string> splitCsvRow(const std::string& line) {
    std::vector<std::string> fields;
    std::stringstream stream(line);
    std::string cell;

    while (std::getline(stream, cell, ',')) {
        fields.push_back(cell);
    }

    return fields;
}

} // namespace

std::vector<ReplayEvent> parseCsvFile(const std::string& path) {
    std::ifstream input(path);
    if (!input.is_open()) {
        throw std::runtime_error("Failed to open input file: " + path);
    }

    std::string header;
    if (!std::getline(input, header)) {
        throw std::runtime_error("Input file is empty: " + path);
    }

    const std::string trimmed_header = trim(header);
    if (trimmed_header != "timestamp,action,order_id,side,type,price,quantity") {
        throw std::runtime_error("Unexpected CSV header in " + path + ": " + trimmed_header);
    }

    std::vector<ReplayEvent> events;
    std::string line;
    size_t line_number = 1;
    while (std::getline(input, line)) {
        ++line_number;
        if (trim(line).empty()) {
            continue;
        }

        const std::vector<std::string> fields = splitCsvRow(line);
        if (fields.size() != 7) {
            throwParseError(line_number, "Expected 7 fields but found " + std::to_string(fields.size()));
        }

        const uint64_t timestamp = parseUnsigned(fields[0], line_number, "timestamp");
        const EventAction action = parseAction(fields[1], line_number);
        const uint64_t order_id = parseUnsigned(fields[2], line_number, "order_id");
        const Side side = parseSide(fields[3], line_number);
        const OrderType type = parseOrderType(fields[4], line_number);
        const int price = parseSignedInt(fields[5], line_number, "price");
        const int quantity = parseSignedInt(fields[6], line_number, "quantity");

        if (order_id == 0) {
            throwParseError(line_number, "order_id must be positive");
        }

        if (action == EventAction::Add) {
            if (quantity <= 0) {
                throwParseError(line_number, "ADD quantity must be positive");
            }

            if (type == OrderType::Limit && price < 0) {
                throwParseError(line_number, "LIMIT price must be non-negative");
            }

            if (type == OrderType::Market && price != 0) {
                throwParseError(line_number, "MARKET price must be 0");
            }
        } else {
            if (price < 0) {
                throwParseError(line_number, "CANCEL price cannot be negative");
            }

            if (quantity < 0) {
                throwParseError(line_number, "CANCEL quantity cannot be negative");
            }
        }

        events.push_back(ReplayEvent{
            timestamp,
            action,
            Order{order_id, side, type, price, quantity, timestamp},
        });
    }

    return events;
}
