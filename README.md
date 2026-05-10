# C++20 Order Matching Engine

Built to understand how order books work at the hardware level — not just the theory. A price-time priority matching engine written in C++20, evolving from a heap-allocated baseline to a price-ladder architecture validated against live market data.

This is a work in progress. The roadmap is honest about what's next.

---

## Architecture Evolution

### v1 — Baseline
Single-threaded, price-time priority core with a custom `std::pmr::monotonic_buffer_resource` memory pool. Zero `malloc` calls on the hot path.

### v2 — Price Ladder + Live Data Validation
Replaced pointer-chased `std::map` price levels with a flat `std::array<Level>` price ladder and a lock-free SPSC ring buffer for order submission. Validated against 27,104 real Binance BTC/USDT Add events captured over 5 minutes of live market feed.

---

## Benchmarks

### v1 — Heap Allocated Baseline
> i5-1334U, GCC 13, `-O3 -march=native`, WSL2 Ubuntu 24.04

| Metric | Value |
|---|---|
| Orders processed | 100,000 |
| Trades executed | ~49,500 |
| Avg latency/order | **2,760 ns** |
| Throughput | **362K orders/sec** |

### v2 — Price Ladder, Real Binance Data
> i5-1334U, GCC 13, `-O3 -march=native`, WSL2 Ubuntu 24.04
> Dataset: 27,104 BTC/USDT Add events, 5 minutes live capture

| Metric | Value |
|---|---|
| Add events | 27,104 |
| Trades executed | 11,948 |
| Avg latency/order | **60.6 ns** |
| Single run throughput | **16.49M orders/sec** |
| Average across 10 runs | **43.59M orders/sec** |

**28x throughput improvement over v1. Validated on real exchange order flow — not synthetic data.**

---

## What it does

**Price-Time Priority** — best bid matches against best ask by price then timestamp. No shortcuts.

**Memory Pool** — `std::pmr::monotonic_buffer_resource` with 1 MiB pre-allocated buffer. Zero heap allocation on hot path.

**Price Ladder (v2)** — flat `std::array<Level>` replaces pointer-chased structures. Cache-friendly, O(1) best bid/ask lookup.

**Live Data Feed** — real Binance BTC/USDT order flow captured via WebSocket, replayed deterministically for benchmarking.

**Profiler** — nanosecond resolution via `std::chrono` with warm-up cycle. Cold cache numbers are meaningless.

**Modular layout** — `Order`, `Trade`, `MemoryPool`, `OrderBook` as swappable components. Benchmark them individually.

---

## Build & Run

**Requirements**
- GCC 13+ (C++20)
- CMake ≥ 3.14
- Linux or WSL2

```bash
git clone https://github.com/adityatomar15/order-matching-engine.git
cd order-matching-engine
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build .
./engine
```

**Run v2 with real data:**
```bash
./v2_real_data data/market_data_adapted.csv
```

---

## Roadmap

- [x] Price-time priority matching core
- [x] `std::pmr` memory pool — zero heap allocation on hot path
- [x] Single-threaded benchmark harness with warm-up cycle
- [x] Price ladder architecture — `std::array<Level>` replacing pointer-chased structures
- [x] Live Binance data feed integration — real market order flow validation
- [ ] Lock-free SPSC ring buffer (`std::atomic`, acquire/release ordering)
- [ ] `alignas(64)` cache line alignment on Order struct — eliminate false sharing
- [ ] `rdtsc` cycle-accurate profiler — replace `std::chrono` on hot path
- [ ] Thread pinning + CPU affinity (`pthread_setaffinity_np`)
- [ ] Cancel and modify order handling
- [ ] Sharded order book — parallel matching across price ranges
- [ ] Target: sub-20 ns/order on optimized hardware

---

## What I learned building this

**v1** forced me to confront the real cost of memory allocation. Profiling revealed `std::allocator` on the hot path was the single biggest latency killer. Implementing `std::pmr::monotonic_buffer_resource` dropped allocation overhead to zero. Understanding placement new and allocator propagation wasn't optional — it was load-bearing.

**v2** taught me that data structure choice matters more than micro-optimization. Replacing `std::map` price levels with a flat `std::array` price ladder produced a 28x throughput improvement — not from clever tricks, but from eliminating pointer chasing and letting the CPU prefetcher do its job. Validating against real Binance data also exposed the gap between synthetic benchmarks and actual market order flow patterns.

The nanosecond profiler exposed how much cold-cache numbers lie. Warm-up cycles aren't optional in serious benchmarking.

---

## What this isn't

This is not a production system. It's a learning project built to understand the real demands of low-latency infrastructure — cache hierarchy, memory ordering, allocation strategies, and honest profiling methodology. Production matching engines also require network stack integration, persistence, risk checks, and concurrent load testing this project doesn't attempt.

---

## AI Use

AI was used for project structure and as a debugging reference. All implementation decisions, architectural choices, benchmarking methodology, and the v2 price ladder redesign are my own.
