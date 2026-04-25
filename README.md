# Limit Order Book Matching Engine

A C++17 limit order book and matching engine that simulates core exchange behavior, including limit orders, market orders, cancellations, price-time priority, trade execution, CSV market data replay, latency benchmarking, top-of-book depth queries, deterministic synthetic order generation, and a static web visualizer for replay traces.

The engine stores bids in descending price order and asks in ascending price order. Each price level keeps a FIFO queue of resting orders, and a hash-indexed order lookup table enables constant-time average cancellation without scanning the book.

## Features

- Limit buy and sell order support with price-time priority matching
- Market buy and sell orders that sweep the opposite side until filled or empty
- O(1) average cancellation via `order_id -> list iterator` lookup
- CSV replay CLI with optional trade export and final book printing
- Visualization trace export plus a polished static web replay viewer in `docs/`
- Benchmark mode with throughput, median, p95, p99, and max latency
- Top-K bid/ask depth queries for interview-style book inspection
- Deterministic synthetic order generator for larger benchmark streams

## Matching Rules

- Better prices execute first.
- Orders resting at the same price execute in FIFO order.
- Trade price is always the resting order's price.
- Market orders never rest on the book.
- Limit orders rest only if they still have remaining quantity after matching.

## Data Structures

- `bids`: `std::map<int, PriceLevel, std::greater<int>>`
- `asks`: `std::map<int, PriceLevel>`
- `PriceLevel`: FIFO `std::list<Order>` plus aggregated quantity at the level
- `order_lookup`: `std::unordered_map<uint64_t, OrderLocation>`

This gives sorted price discovery, stable iterators for in-book orders, and direct cancellation without a linear scan through the book.

## Complexity

- Add resting limit order: `O(log P)`
- Match incoming order: `O(M log P)` worst case
- Cancel order: `O(1)` average lookup + `O(1)` erase
- Space: `O(N)`

Where:

- `N` = active orders
- `P` = active price levels
- `M` = resting orders touched during matching

## Project Layout

```text
.
├── CMakeLists.txt
├── Makefile
├── README.md
├── PRD.md
├── limit_order_book_PRD.md
├── include/
├── src/
├── tests/
├── data/
├── docs/
└── output/
```

## Build

### Makefile fallback

```bash
make all
make test
```

Artifacts are written to `build/bin/`.

### CMake

```bash
cmake -S . -B build/cmake
cmake --build build/cmake
ctest --test-dir build/cmake --output-on-failure
```

## Run

Replay the sample CSV:

```bash
./build/bin/lob --input data/sample_orders.csv
```

Replay with latency benchmarking, book printing, and trade export:

```bash
./build/bin/lob --input data/sample_orders.csv --benchmark --print-book --trades output/trades.csv
```

Export a browser-ready visualization trace:

```bash
./build/bin/lob --input data/sample_orders.csv --benchmark --visualize docs/data/sample_orders.trace.json --visualize-depth 12 --visualize-frame-step 1
```

Generate a larger synthetic dataset:

```bash
./build/bin/generator --events 100000 --output data/synthetic_orders.csv --cancel-prob 0.18 --market-prob 0.08 --volatility 12 --avg-size 75 --seed 4242
```

## Web Visualizer

The repo ships a static replay visualizer in `docs/` that consumes JSON traces exported by the C++ engine.

Generate the bundled sample trace:

```bash
./build/bin/lob --input data/sample_orders.csv --benchmark --visualize docs/data/sample_orders.trace.json
```

Serve the repo locally and open the demo:

```bash
python3 -m http.server 4173
```

Then visit:

```text
http://127.0.0.1:4173/docs/
```

The visualizer includes:

- Timeline scrubbing and autoplay controls
- Mirrored bid/ask depth ladders with trade highlights
- Event-by-event execution detail and trade tape
- Spread and cumulative volume sparkline charts
- Local trace upload for custom JSON exports

The `docs/` folder is GitHub Pages-ready, so the same assets can be published directly from the repository when Pages is enabled.

## Example Input

```csv
timestamp,action,order_id,side,type,price,quantity
1,ADD,1001,BUY,LIMIT,10050,200
2,ADD,1002,SELL,LIMIT,10070,100
3,ADD,1003,SELL,LIMIT,10060,120
4,ADD,1004,BUY,LIMIT,10065,150
5,CANCEL,1001,BUY,LIMIT,10050,200
6,ADD,1005,SELL,MARKET,0,20
7,ADD,1006,BUY,MARKET,0,70
8,ADD,1007,BUY,LIMIT,10055,40
```

## Example Output

Representative output from one verified run of `./build/bin/lob --input data/sample_orders.csv --benchmark --trades output/trades.csv --print-book`:

```text
Processed 8 events
Generated 3 trades
Active orders: 3
Best bid: 100.65
Best ask: 100.70
Total runtime: 0.003 ms
Throughput: 2999625.05 events/sec
Median latency: 0.21 us
p95 latency: 0.54 us
p99 latency: 0.54 us
Max latency: 0.54 us
Trades written to: output/trades.csv

ASKS
100.70 | 30

BIDS
100.65 | 10
100.55 | 40
```

## Benchmark Snapshot

Representative benchmark snapshot from one verified run on this machine using a generated 100,000-event CSV:

```text
Processed 100,000 events
Generated 63,418 trades
Active orders: 48
Best bid: 57.16
Best ask: 57.27
Total runtime: 11.717 ms
Throughput: 8534486.92 events/sec
Median latency: 0.08 us
p95 latency: 0.21 us
p99 latency: 0.29 us
Max latency: 87.38 us
```

These numbers are illustrative, not a universal performance guarantee. They depend on compiler settings, hardware, and the generated order flow.

## Testing

The test suite covers:

- Resting limit orders
- Full fills and partial fills
- Price-time priority
- Better-price priority
- Successful and missing cancels
- Market order sweeps and empty-book handling
- Price-level cleanup and active order counts
- Duplicate active ID rejection
- Top-K bid and ask depth queries
- Sample CSV replay integration
- Synthetic generator parseability and replay correctness

Run the tests with either `make test` or `ctest --test-dir build/cmake --output-on-failure`.

## Future Improvements

- Add a simple PnL and inventory simulator on top of replayed fills
- Explore multithreaded replay with explicit queue boundaries
- Introduce binary market data formats or mmap-based input for larger datasets
- Add property-based tests for random order streams and replay invariants
