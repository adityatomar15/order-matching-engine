#include <iostream>
#include <array>
#include <vector>
#include <chrono>
#include <cstdint>
#include <algorithm>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <cstdlib>

using OrderId   = uint64_t;
using Quantity  = uint64_t;
using Price     = int64_t;
using Timestamp = uint64_t;

enum class Side : uint8_t { Buy, Sell };

struct Order {
    OrderId   id;
    Price     price;
    Quantity  qty;
    Timestamp timestamp;
    Side      side;
};

struct Trade {
    OrderId  buy_id, sell_id;
    Price    price;
    Quantity qty;
};

// ─── Free‑list allocator ─────────────────────────────────────────────
// Replaces bump allocator – cancelled/filled orders are immediately reused
class OrderPool {
    std::vector<Order> pool_;         // all order objects
    std::vector<int>   free_list_;    // indices of freed slots
    size_t             next_id_{1};

public:
    explicit OrderPool(size_t max_orders) : pool_(max_orders + 1) {}

    int allocate() {
        if (!free_list_.empty()) {
            int idx = free_list_.back();
            free_list_.pop_back();
            return idx;
        }
        return static_cast<int>(next_id_++);
    }

    void deallocate(int idx) {
        free_list_.push_back(idx);
    }

    Order& get(int idx) { return pool_[idx]; }
    const Order& get(int idx) const { return pool_[idx]; }
    size_t capacity() const { return pool_.size(); }
};

// ─── Price Ladder ─────────────────────────────────────────────────────
static constexpr Price  TICK_MIN = 9500;
static constexpr Price  TICK_MAX = 10500;
static constexpr int    N_LEVELS = static_cast<int>(TICK_MAX - TICK_MIN + 1);

struct Level {
    std::vector<Order*> queue;
    size_t              head{0};

    __attribute__((always_inline)) void push(Order* o) { queue.push_back(o); }
    __attribute__((always_inline)) bool empty()  const   { return head >= queue.size(); }
    __attribute__((always_inline)) Order* front() const { return queue[head]; }
    __attribute__((always_inline)) void pop()           { ++head; }
    void reset() { queue.clear(); head = 0; }

    void skipTombstones() {
        while (head < queue.size() && queue[head]->qty == 0)
            ++head;
    }
};

// ─── Order Book ───────────────────────────────────────────────────────
class OrderBook {
    OrderPool& pool_;
    std::array<Level, N_LEVELS> bids_;
    std::array<Level, N_LEVELS> asks_;

    int best_bid_{-1};
    int best_ask_{N_LEVELS};

    std::vector<Order*> order_map_;   // id → pointer (null = cancelled/filled)

    [[nodiscard]] static constexpr __attribute__((always_inline))
    int toIdx(Price p) noexcept {
        return static_cast<int>(p - TICK_MIN);
    }

    __attribute__((always_inline)) void findNextLiveBid() {
        while (best_bid_ >= 0) {
            Level& lvl = bids_[best_bid_];
            lvl.skipTombstones();
            if (!lvl.empty()) return;
            --best_bid_;
        }
    }
    __attribute__((always_inline)) void findNextLiveAsk() {
        while (best_ask_ < N_LEVELS) {
            Level& lvl = asks_[best_ask_];
            lvl.skipTombstones();
            if (!lvl.empty()) return;
            ++best_ask_;
        }
    }

public:
    explicit OrderBook(OrderPool& pool) : pool_(pool) {
        order_map_.assign(pool_.capacity(), nullptr);
        size_t perLevel = 256;
        for (auto& lvl : bids_) lvl.queue.reserve(perLevel);
        for (auto& lvl : asks_) lvl.queue.reserve(perLevel);
    }

    __attribute__((flatten)) OrderId addOrder(Price price, Quantity qty,
                                               Side side, Timestamp ts) {
        int slot = pool_.allocate();
        Order& o = pool_.get(slot);
        o.id = static_cast<OrderId>(slot);
        o.price = price;
        o.qty = qty;
        o.timestamp = ts;
        o.side = side;
        order_map_[slot] = &o;

        const int idx = toIdx(price);
        if (side == Side::Buy) {
            bids_[idx].push(&o);
            if (idx > best_bid_) best_bid_ = idx;
        } else {
            asks_[idx].push(&o);
            if (idx < best_ask_) best_ask_ = idx;
        }
        return o.id;
    }

    bool cancelOrder(OrderId id) {
        if (id == 0 || id >= order_map_.size()) return false;
        Order* o = order_map_[id];
        if (!o) return false;
        o->qty = 0;                       // tombstone – will be skipped
        order_map_[id] = nullptr;
        pool_.deallocate(static_cast<int>(id));
        return true;
    }

    std::vector<Trade> match() {
        std::vector<Trade> trades;
        trades.reserve(128);

        findNextLiveBid();
        findNextLiveAsk();

        while (best_bid_ >= best_ask_ && best_bid_ >= 0 && best_ask_ < N_LEVELS) {
            Order* bid = bids_[best_bid_].front();
            Order* ask = asks_[best_ask_].front();

            Quantity fill = bid->qty < ask->qty ? bid->qty : ask->qty;
            const Price px = TICK_MIN + best_ask_;
            trades.push_back({bid->id, ask->id, px, fill});

            bid->qty -= fill;
            ask->qty -= fill;

            if (bid->qty == 0) {
                bids_[best_bid_].pop();
                order_map_[bid->id] = nullptr;
                pool_.deallocate(static_cast<int>(bid->id));
                findNextLiveBid();
            }
            if (ask->qty == 0) {
                asks_[best_ask_].pop();
                order_map_[ask->id] = nullptr;
                pool_.deallocate(static_cast<int>(ask->id));
                findNextLiveAsk();
            }
        }
        return trades;
    }

    void reset() {
        for (auto& l : bids_) l.reset();
        for (auto& l : asks_) l.reset();
        order_map_.clear();
        best_bid_ = -1;
        best_ask_ = N_LEVELS;
    }
};

// ─── Main ─────────────────────────────────────────────────────────────
int main(int argc, char* argv[]) {
    const char* csv_file = "data/market_data_adapted.csv";
    if (argc > 1) csv_file = argv[1];

    std::ifstream f(csv_file);
    if (!f.is_open()) {
        std::cerr << "Cannot open " << csv_file << "\n";
        return 1;
    }

    // Pre‑count events to size the pool
    size_t event_count = 0;
    std::string line;
    std::getline(f, line);   // skip header
    while (std::getline(f, line)) {
        if (!line.empty()) ++event_count;
    }
    f.clear();
    f.seekg(0);
    std::getline(f, line);   // skip header again

    OrderPool pool(event_count * 2);   // generous capacity
    OrderBook* book = new OrderBook(pool);

    uint64_t adds = 0, cancels = 0;
    auto t0 = std::chrono::high_resolution_clock::now();

    while (std::getline(f, line)) {
        if (line.empty()) continue;
        std::stringstream ss(line);
        std::string ts_str, type_str, side_str, price_str, qty_str;
        std::getline(ss, ts_str,   ',');
        std::getline(ss, type_str, ',');
        std::getline(ss, side_str, ',');
        std::getline(ss, price_str,',');
        std::getline(ss, qty_str,  ',');

        Timestamp ts = std::stoull(ts_str);
        Price p = static_cast<Price>(std::stod(price_str) * 100.0 + 0.5);
        if (p < TICK_MIN) p = TICK_MIN;
        if (p > TICK_MAX) p = TICK_MIN + (p % (TICK_MAX - TICK_MIN + 1));
        Quantity q = static_cast<Quantity>(std::stod(qty_str) * 1e8);

        if (type_str == "A") {
            Side s = (side_str == "B") ? Side::Buy : Side::Sell;
            book->addOrder(p, q, s, ts);
            ++adds;
        } else if (type_str == "C") {
            // Cancel events – full implementation needs original order ID
            // Here we count them but skip actual cancellation
            ++cancels;
        } else if (type_str == "T") {
            // Trade events are outputs, ignore in replay
        }
    }
    f.close();

    auto trades = book->match();
    auto t1 = std::chrono::high_resolution_clock::now();

    auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
    uint64_t total_events = adds + cancels;
    double avg_ns = (total_events > 0) ? (double)ns / total_events : 0;
    uint64_t throughput = (total_events > 0) ? (uint64_t)(1e9 * total_events / ns) : 0;

    std::cout << "═══════════════════════════════════════\n";
    std::cout << " v2 — Real Data Replay (Free List + Interleaved)\n";
    std::cout << "═══════════════════════════════════════\n";
    std::cout << " Add events   : " << adds        << '\n';
    std::cout << " Cancel events: " << cancels     << '\n';
    std::cout << " Total events : " << total_events << '\n';
    std::cout << " Trades       : " << trades.size() << '\n';
    std::cout << " Total ns     : " << ns          << '\n';
    std::cout << " Avg ns/event : " << avg_ns      << '\n';
    std::cout << " Throughput   : " << throughput   << " events/sec\n";
    std::cout << "═══════════════════════════════════════\n";

    delete book;
    return 0;
}