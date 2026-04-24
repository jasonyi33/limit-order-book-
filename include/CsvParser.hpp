#pragma once

#include <string>
#include <vector>

#include "Order.hpp"

enum class EventAction {
    Add,
    Cancel
};

struct ReplayEvent {
    uint64_t timestamp;
    EventAction action;
    Order order;
};

std::vector<ReplayEvent> parseCsvFile(const std::string& path);
