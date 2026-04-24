# PRD: C++ Limit Order Book Matching Engine

## 1. Project Summary

Build a C++17 limit order book matching engine that simulates the core logic of an electronic exchange.

The system should support:

- Limit buy and sell orders
- Market buy and sell orders
- Order cancellation
- Price-time priority matching
- Trade execution logs
- CSV market data replay
- Throughput and latency benchmarking

The goal is to create a resume-ready quant dev project that shows strong skills in:

- C++
- Data structures
- Algorithms
- Low-latency systems
- Performance measurement
- Financial market mechanics

## 2. Resume Bullet Target

Use this after the project is working and pushed to GitHub:

```latex
\resumeProjectHeading
  {\textbf{Limit Order Book Matching Engine} $|$ \emph{C++17, Trading Systems, Low-Latency Infrastructure} $|$ \href{https://github.com/jasonyi33/limit-order-book}{\underline{Link}}}{Apr 2026}
  \resumeItemListStart
\resumeItem{Built C++17 matching engine supporting limit orders, market orders, cancellations, and price-time priority execution.}
\resumeItem{Designed order book with ordered price levels, FIFO queues per level, and hash-indexed order IDs for constant-time cancellation.}
\resumeItem{Implemented market data replay and benchmarking harness to measure throughput, median latency, p99 latency, and book depth.}
  \resumeItemListEnd
```

## 3. Core Concept

A limit order book has two sides:

- **Bid book:** buy orders, sorted from highest price to lowest price.
- **Ask book:** sell orders, sorted from lowest price to highest price.

An incoming order matches against the opposite side of the book.

Example current ask book:

```text
Price 101.00: Sell 50 shares
Price 102.00: Sell 70 shares
```

Incoming order:

```text
Buy 80 shares at limit price 102.00
```

The engine should:

1. Match 50 shares at 101.00.
2. Match 30 shares at 102.00.
3. Leave 40 shares remaining at 102.00.

Trades execute using price-time priority:

1. Better prices execute first.
2. For the same price, older orders execute first.

## 4. MVP Requirements

### 4.1 Order Types

The engine must support these actions:

#### Limit Buy

A buy order with a maximum price.

Example:

```text
BUY 100 shares @ 50.25
```

It can match with sell orders priced at or below 50.25.

#### Limit Sell

A sell order with a minimum price.

Example:

```text
SELL 100 shares @ 50.25
```

It can match with buy orders priced at or above 50.25.

#### Market Buy

A buy order with no price limit.

It consumes the cheapest available sell orders until fully filled or the ask book is empty.

#### Market Sell

A sell order with no price limit.

It consumes the highest available buy orders until fully filled or the bid book is empty.

#### Cancel

Remove an active order by order ID.

Example:

```text
CANCEL order_id=12345
```

## 5. Data Model

### 5.1 Enums

```cpp
enum class Side {
    Buy,
    Sell
};

enum class OrderType {
    Limit,
    Market
};
```

### 5.2 Order

```cpp
struct Order {
    uint64_t id;
    Side side;
    OrderType type;
    int price;       // store price in cents
    int quantity;
    uint64_t timestamp;
};
```

Use integer prices, not floating point.

Example:

```text
$101.25 -> 10125
```

This avoids floating-point precision errors.

### 5.3 Trade

```cpp
struct Trade {
    uint64_t buy_order_id;
    uint64_t sell_order_id;
    int price;
    int quantity;
    uint64_t timestamp;
};
```

## 6. Main Data Structures

### 6.1 Price Levels

Use ordered maps for price levels.

```cpp
std::map<int, std::list<Order>, std::greater<int>> bids;
std::map<int, std::list<Order>> asks;
```

Why:

- Bids need highest price first.
- Asks need lowest price first.
- `std::map` gives sorted price levels.
- `std::list` gives stable iterators and O(1) deletion.

### 6.2 Order Lookup Table

Use a hash map to cancel orders quickly.

```cpp
struct OrderLocation {
    Side side;
    int price;
    std::list<Order>::iterator it;
};

std::unordered_map<uint64_t, OrderLocation> order_lookup;
```

Why:

- Without this, cancellation requires scanning the book.
- With this, cancellation can find the order in O(1) average time.

## 7. Matching Algorithm

### 7.1 Limit Buy

A limit buy matches while:

```text
ask book is not empty
AND best ask price <= buy limit price
AND incoming quantity > 0
```

At each step:

1. Get best ask level.
2. Take the oldest sell order at that price.
3. Match `min(incoming quantity, resting quantity)`.
4. Create a trade.
5. Decrease quantities.
6. Remove filled orders.
7. If incoming order still has quantity, continue matching.
8. If it still has remaining quantity after matching, add it to the bid book.

### 7.2 Limit Sell

A limit sell matches while:

```text
bid book is not empty
AND best bid price >= sell limit price
AND incoming quantity > 0
```

Then follow the same process against the bid book.

### 7.3 Market Buy

A market buy matches while:

```text
ask book is not empty
AND incoming quantity > 0
```

It does not check price.

### 7.4 Market Sell

A market sell matches while:

```text
bid book is not empty
AND incoming quantity > 0
```

It does not check price.

## 8. Complexity

Let:

- `N` = number of active orders
- `P` = number of active price levels
- `M` = number of resting orders matched by an incoming order

### Add Resting Limit Order

```text
Time: O(log P)
Space: O(1)
```

The engine finds or creates the price level in the map, then appends to the FIFO queue.

### Match Incoming Order

```text
Time: O(M log P) worst case
Space: O(M) for generated trades
```

Each filled price level may require a map erase. In practice, matching is close to O(M) when most work happens at the best price level.

### Cancel Order

```text
Time: O(1) average lookup + O(1) list erase
Space: O(1)
```

The hash map points directly to the order's location.

### Total Space

```text
Space: O(N)
```

The book stores all active orders and one hash-map entry per active order.

## 9. Public API

Create a class called `OrderBook`.

```cpp
class OrderBook {
public:
    std::vector<Trade> addOrder(const Order& order);
    bool cancelOrder(uint64_t order_id);

    int getBestBid() const;
    int getBestAsk() const;

    int getBidDepth(int price) const;
    int getAskDepth(int price) const;

    size_t activeOrderCount() const;
    void printBook() const;
};
```

### 9.1 `addOrder`

Adds a new order.

Returns all trades generated by that order.

```cpp
std::vector<Trade> trades = book.addOrder(order);
```

### 9.2 `cancelOrder`

Cancels an active order.

Returns:

```text
true if the order was found and canceled
false otherwise
```

### 9.3 `getBestBid`

Returns highest active bid price.

If no bid exists, return `-1`.

### 9.4 `getBestAsk`

Returns lowest active ask price.

If no ask exists, return `-1`.

### 9.5 `getBidDepth`

Returns total quantity at a bid price.

### 9.6 `getAskDepth`

Returns total quantity at an ask price.

### 9.7 `activeOrderCount`

Returns number of active resting orders.

### 9.8 `printBook`

Prints the top levels of the book.

Example:

```text
ASKS
101.20 | 300
101.10 | 150

BIDS
101.00 | 200
100.90 | 500
```

## 10. Input Format

Support a CSV file for market data replay.

Example file:

```csv
timestamp,action,order_id,side,type,price,quantity
1,ADD,1001,BUY,LIMIT,10050,200
2,ADD,1002,SELL,LIMIT,10070,100
3,ADD,1003,BUY,LIMIT,10080,150
4,CANCEL,1001,BUY,LIMIT,10050,200
5,ADD,1004,SELL,MARKET,0,75
```

### Fields

| Field | Meaning |
|---|---|
| `timestamp` | Order timestamp |
| `action` | `ADD` or `CANCEL` |
| `order_id` | Unique order ID |
| `side` | `BUY` or `SELL` |
| `type` | `LIMIT` or `MARKET` |
| `price` | Price in cents |
| `quantity` | Number of shares/contracts |

For market orders, price should be `0`.

## 11. CLI Requirements

Build a simple command-line program.

Example:

```bash
./lob --input data/orders.csv
```

Output:

```text
Processed 1,000,000 events
Generated 247,193 trades
Active orders: 83,421
Best bid: 101.25
Best ask: 101.26
Throughput: 1,850,000 events/sec
Median latency: 0.42 us
p99 latency: 2.10 us
```

Optional commands:

```bash
./lob --input data/orders.csv --print-book
./lob --input data/orders.csv --benchmark
./lob --input data/orders.csv --trades output/trades.csv
```

## 12. Benchmarking Requirements

The benchmarking harness should measure:

- Total events processed
- Total trades generated
- Total runtime
- Throughput in events/sec
- Median latency
- p95 latency
- p99 latency
- Maximum latency

Use:

```cpp
std::chrono::high_resolution_clock
```

For each event:

1. Start timer.
2. Process order or cancel.
3. Stop timer.
4. Store duration in nanoseconds.

At the end:

1. Sort latencies.
2. Report median, p95, p99, and max.

## 13. Testing Requirements

Use simple unit tests first. You can use `assert` or GoogleTest.

### Test 1: Add Limit Order

Input:

```text
BUY LIMIT 100 @ 50.00
```

Expected:

```text
No trades
Best bid = 50.00
Active orders = 1
```

### Test 2: Basic Match

Input:

```text
SELL LIMIT 100 @ 50.00
BUY LIMIT 100 @ 50.00
```

Expected:

```text
One trade
Trade price = 50.00
Trade quantity = 100
Book empty
```

### Test 3: Partial Fill

Input:

```text
SELL LIMIT 100 @ 50.00
BUY LIMIT 40 @ 50.00
```

Expected:

```text
One trade of 40
Remaining sell quantity = 60
Best ask = 50.00
```

### Test 4: Price-Time Priority

Input:

```text
SELL LIMIT 100 @ 50.00, id=1
SELL LIMIT 100 @ 50.00, id=2
BUY LIMIT 150 @ 50.00, id=3
```

Expected:

```text
Trade with id=1 for 100
Trade with id=2 for 50
Order id=2 has 50 remaining
```

### Test 5: Better Price First

Input:

```text
SELL LIMIT 100 @ 50.10
SELL LIMIT 100 @ 50.00
BUY LIMIT 100 @ 50.10
```

Expected:

```text
Buy matches 50.00 sell first
```

### Test 6: Cancel Order

Input:

```text
BUY LIMIT 100 @ 50.00, id=1
CANCEL id=1
```

Expected:

```text
Cancel returns true
Book is empty
```

### Test 7: Cancel Missing Order

Input:

```text
CANCEL id=999
```

Expected:

```text
Cancel returns false
```

### Test 8: Market Order

Input:

```text
SELL LIMIT 100 @ 50.00
SELL LIMIT 100 @ 50.10
BUY MARKET 150
```

Expected:

```text
Trade 100 @ 50.00
Trade 50 @ 50.10
Remaining sell 50 @ 50.10
```

## 14. Folder Structure

Use this structure:

```text
limit-order-book/
├── README.md
├── PRD.md
├── CMakeLists.txt
├── include/
│   ├── Order.hpp
│   ├── Trade.hpp
│   ├── OrderBook.hpp
│   └── CsvParser.hpp
├── src/
│   ├── OrderBook.cpp
│   ├── CsvParser.cpp
│   ├── Benchmark.cpp
│   └── main.cpp
├── tests/
│   └── OrderBookTests.cpp
├── data/
│   ├── sample_orders.csv
│   └── synthetic_orders.csv
└── output/
    └── trades.csv
```

## 15. Milestones

### Milestone 1: Basic Order Book

Goal: Add resting limit orders.

Build:

- `Order`
- `Trade`
- `OrderBook`
- Bid and ask maps
- `addOrder`
- `getBestBid`
- `getBestAsk`
- `printBook`

Acceptance criteria:

- Can add buy and sell limit orders.
- Book prints in correct price order.
- Best bid and best ask return correct values.

### Milestone 2: Matching Engine

Goal: Match incoming limit orders.

Build:

- Limit buy matching
- Limit sell matching
- Trade generation
- Partial fills
- Full fills
- Price-time priority

Acceptance criteria:

- Basic matching test passes.
- Partial fill test passes.
- Price-time priority test passes.
- Better price priority test passes.

### Milestone 3: Cancellation

Goal: Support fast cancellation.

Build:

- `OrderLocation`
- `order_lookup`
- `cancelOrder`

Acceptance criteria:

- Cancel active order works.
- Cancel missing order returns false.
- Canceling an order removes it from the book and lookup table.
- Empty price levels are erased.

### Milestone 4: Market Orders

Goal: Support market buy and sell orders.

Build:

- Market buy matching
- Market sell matching
- Empty-book handling

Acceptance criteria:

- Market orders consume opposite side until filled or book is empty.
- Market orders never rest on the book.
- No crash on empty book.

### Milestone 5: CSV Replay

Goal: Process many events from a file.

Build:

- CSV parser
- Replay function
- Trade output file

Acceptance criteria:

- Program reads `sample_orders.csv`.
- Program processes `ADD` and `CANCEL`.
- Program writes executed trades to `output/trades.csv`.

### Milestone 6: Benchmarking

Goal: Measure performance.

Build:

- Event-level latency timer
- Throughput calculation
- Median, p95, p99 latency reporting

Acceptance criteria:

- Program reports total runtime.
- Program reports events/sec.
- Program reports median, p95, p99, and max latency.

### Milestone 7: README and Resume Polish

Goal: Make the repo look serious.

README should include:

- What the project does
- Matching rules
- Data structures
- Complexity analysis
- Build instructions
- Example input
- Example output
- Benchmark results
- Future improvements

## 16. Stretch Features

Only add these after the MVP works.

### Stretch 1: Top-K Book Depth

Return top 5 or top 10 price levels.

```cpp
std::vector<std::pair<int, int>> getTopBids(int k);
std::vector<std::pair<int, int>> getTopAsks(int k);
```

### Stretch 2: Synthetic Order Generator

Generate random order flow.

Parameters:

- Number of events
- Cancel probability
- Market order probability
- Price volatility
- Average order size

Example:

```bash
./generator --events 1000000 --output data/synthetic_orders.csv
```

### Stretch 3: PnL Simulator

Add a simple market-making strategy.

Example:

- Place bid below midprice.
- Place ask above midprice.
- Track inventory and PnL.

This makes the project more quant-facing, but only do it after the engine works.

### Stretch 4: Multithreaded Replay

Separate:

- CSV reading thread
- Matching engine thread
- Output writing thread

Use a queue between them.

This is harder and can introduce bugs. Only add it if you can explain it well.

## 17. Non-Goals

Do not build these for the first version:

- Real exchange connectivity
- Real-time market data feeds
- Options pricing
- Web dashboard
- Machine learning
- Blockchain
- Complex trading strategies
- Distributed systems

Keep the project focused. A clean, correct matching engine is better than a messy trading platform.

## 18. Definition of Done

The project is done when:

- All core order types work.
- All tests pass.
- CSV replay works.
- Benchmarking works.
- README explains data structures and complexity.
- GitHub repo has clean commits.
- Resume bullets are truthful.
- You can explain every line in an interview.

## 19. Suggested README Summary

Use this at the top of the repo:

```markdown
# Limit Order Book Matching Engine

A C++17 limit order book and matching engine that simulates core exchange behavior, including limit orders, market orders, cancellations, price-time priority, trade execution, CSV market data replay, and latency benchmarking.

The engine stores bid and ask books as ordered price levels with FIFO queues per level. It maintains a hash-indexed order lookup table for constant-time average cancellation and reports throughput, median latency, p95 latency, and p99 latency across replayed order streams.
```

## 20. Interview Explanation

Say this:

> I built a C++ matching engine that simulates the core of an exchange. The book keeps bids sorted high-to-low and asks sorted low-to-high. Each price level stores orders in FIFO order, so matching follows price-time priority. I also maintain a hash map from order ID to list iterator, which makes cancellation O(1) average time instead of requiring a scan through the book. I added a replay harness and benchmarked event throughput, median latency, and p99 latency across synthetic order streams.
