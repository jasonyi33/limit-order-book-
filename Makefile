CXX := clang++
CXXFLAGS := -std=c++17 -Wall -Wextra -Wpedantic -O2 -Iinclude
BIN_DIR := build/bin

COMMON_SRCS := src/Benchmark.cpp src/CsvParser.cpp src/OrderBook.cpp src/SyntheticOrderGenerator.cpp

.PHONY: all clean test

all: lob generator lob_tests

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

lob: $(BIN_DIR)
	$(CXX) $(CXXFLAGS) $(COMMON_SRCS) src/main.cpp -o $(BIN_DIR)/lob

generator: $(BIN_DIR)
	$(CXX) $(CXXFLAGS) $(COMMON_SRCS) src/generator_main.cpp -o $(BIN_DIR)/generator

lob_tests: $(BIN_DIR)
	$(CXX) $(CXXFLAGS) $(COMMON_SRCS) tests/OrderBookTests.cpp -o $(BIN_DIR)/lob_tests

test: lob_tests
	./$(BIN_DIR)/lob_tests

clean:
	rm -rf build/bin
