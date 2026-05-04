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

2,760 ns is slow for HFT standards (production engines run 50–200 ns "said by AI"). 
I know exactly why — mutex overhead on the threading skeleton, cache misses on price level lookups, 
and the SPSC ring buffer isn't integrated yet. The roadmap below is the fix ("AI HELPED ME ON IDENTIFYING THESE ISSUES")

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
git clone https://github.com/[YOUR_USERNAME]/order-matching-engine.git
cd order-matching-engine
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build .
./engine
```

---

## What I learned building this

The memory model stuff was the hardest part. Writing the SPSC buffer wasn't difficult — understanding *why* 
`memory_order_acquire` on the load and `memory_order_release` on the store is the correct pairing, and what happens on x86 vs ARM if you get it wrong,
took considerably longer. Still debugging the integration.

The memory pool was surprisingly satisfying. Watching `malloc` calls disappear from the profiler output after switching to `pmr` was a good moment.

---

## What this isn't

This is not a production system. It has no network layer, no FIX protocol, no persistence. It's a learning project built to understand the *engineering constraints*
of low-latency systems — cache hierarchy, memory ordering, allocation strategies, and profiling methodology.

The goal is to close the gap between "my current skills" and the " skills i want to have "

## AI USE AND HOW MUCH 
yes i used ai for this project as it was my first project that i made at this level 
AI was used for project file Architecture, and what type of functions and lines should be written by me.
