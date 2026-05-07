# C++ Order Matching Engine

I built this to actually understand how order books work at the hardware level — not just the theory. It's a single-threaded, price-time priority matching engine written in C++20, with a custom memory pool so the hot path never touches the heap.

This is a work in progress. The roadmap is honest about what's next.

---

## What it does

Takes a stream of limit orders (buy/sell, price, quantity), matches them by price-time priority, and spits out trades. Standard stuff — but the interesting part is the infrastructure underneath.

**Price-Time Priority** — best bid (highest price, earliest timestamp) matches against best ask (lowest price, earliest timestamp). No shortcuts.

**Memory Pool** — using `std::pmr::monotonic_buffer_resource` with a 1 MiB pre-allocated buffer. Orders are placed directly into the pool at startup. Zero `malloc` calls on the hot path. This was the most educational part to build — understanding placement new, allocator propagation, and why the default allocator is a latency killer.

**Profiler** — nanosecond resolution via `std::chrono`, with a warm-up cycle before measurement starts (cold cache numbers are meaningless).

**Modular layout** — `Order`, `Trade`, `MemoryPool`, `OrderBook` are separate classes. Makes it easier to swap components and benchmark them individually.

---

## Benchmark — First Iteration

> Compiled with `-O3 -march=native` on Intel i7-12700H, WSL2 Ubuntu 24.04, GCC 13, C++20.

| Metric                | Value                   |
|---                    |---                      |
| Orders processed      | 100,000                 |
| Trades executed       | ~49,500                 |
| Total time            | 276 ms                  |
| Avg latency per order | **2,760 ns (2.76 µs)**  |
| Throughput            | **~362,000 orders/sec** |


---

## Roadmap

- [x] Price-time priority matching core
- [x] `std::pmr` memory pool — zero heap allocation on hot path
- [x] Single-threaded benchmark harness
- [x] Multi-threaded submission skeleton (`std::mutex` / `std::condition_variable`)
- [ ] Integrate lock-free SPSC ring buffer (`std::atomic`, acquire/release ordering)
- [ ] `alignas(64)` cache line alignment on Order struct — eliminate false sharing
- [ ] Flat pre-allocated price level arrays — replace pointer-chased structures
- [ ] `rdtsc` cycle-accurate profiler — replace `std::chrono` on hot path
- [ ] Thread pinning + CPU affinity (`pthread_setaffinity_np`)
- [ ] Sharded order book (parallel matching across price ranges)
- [ ] Target: sub-500 ns per order, 1M+ orders/sec

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

---

## What I learned building this

Building this forced me to confront the real cost of memory allocation. Profiling revealed that std::allocator on the hot path was the single biggest latency killer — implementing std::pmr::monotonic_buffer_resource dropped allocation overhead to zero. Understanding placement new and allocator propagation wasn't optional, it was load-bearing. The nanosecond profiler exposed how much cold-cache numbers lie — warm-up cycles aren't optional in serious benchmarking.

---

## What this isn't

This is not a production system. It's a learning project built to understand the *real world use in complex tasks*
of low-latency systems — cache hierarchy, memory ordering, allocation strategies, and profiling methodology.

This is not a production system. It's a learning project built to understand the real demands of low-latency infrastructure — cache hierarchy, memory ordering, allocation strategies, and honest profiling methodology

## AI USE AND HOW MUCH 
Yes i used ai for this project as it was my first project that i made at this level. 
AI was used for project structure and as a debugging reference. All implementation decisions, benchmarking methodology, and architectural choices are my own
