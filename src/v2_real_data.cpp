#include <iostream>
#include <array>
#include <vector>
#include <chrono>
#include <random>
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

// ─── Aligned Bump Allocator ──────────────────────────────────────────
class MemoryPool {
    void*  buffer_;
    size_t capacity_;
    size_t offset_{0};

public:
    explicit MemoryPool(size_t size) : capacity_(size) {
        buffer_ = std::aligned_alloc(alignof(std::max_align_t), size);
        if (!buffer_) throw std::bad_alloc();
        std::memset(buffer_, 0, size);
    }

    ~MemoryPool() { std::free(buffer_); }

    MemoryPool(const MemoryPool&) = delete;
    MemoryPool& operator=(const MemoryPool&) = delete;

    template<typename T, typename... Args>
    [[nodiscard]] __attribute__((always_inline))
    T* allocate(Args&&... args) {
        constexpr size_t alignment = alignof(T);
        size_t size = sizeof(T);
        size_t aligned = (offset_ + alignment - 1) & ~(alignment - 1);
        if (__builtin_expect(aligned + size > capacity_, 0))
            throw std::bad_alloc();
        void* mem = static_cast<char*>(buffer_) + aligned;
        offset_ = aligned + size;
        return ::new (mem) T{std::forward<Args>(args)...};
    }
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
    MemoryPool& pool_;
    OrderId     next_id_{1};

    std::array<Level, N_LEVELS> bids_;
    std::array<Level, N_LEVELS> asks_;

    int best_bid_{-1};
    int best_ask_{N_LEVELS};

    std::vector<Order*> order_map_;

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
    explicit OrderBook(MemoryPool& pool) : pool_(pool) {}

    void prepare(size_t expectedOrders) {
        order_map_.assign(expectedOrders, nullptr);
        size_t perLevel = expectedOrders / N_LEVELS + 128;
        for (auto& lvl : bids_) lvl.queue.reserve(perLevel);
        for (auto& lvl : asks_) lvl.queue.reserve(perLevel);
    }

    __attribute__((flatten)) OrderId addOrder(Price price, Quantity qty,
                                               Side side, Timestamp ts) {
        const OrderId id = next_id_++;
        Order* o = pool_.allocate<Order>(id, price, qty, ts, side);
        order_map_[id - 1] = o;

        const int idx = toIdx(price);
        if (side == Side::Buy) {
            bids_[idx].push(o);
            if (idx > best_bid_) best_bid_ = idx;
        } else {
            asks_[idx].push(o);
            if (idx < best_ask_) best_ask_ = idx;
        }
        return id;
    }

    bool cancelOrder(OrderId id) {
        if (id == 0 || id > order_map_.size()) return false;
        Order* o = order_map_[id - 1];
        if (!o) return false;
        o->qty = 0;
        order_map_[id - 1] = nullptr;
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
                order_map_[bid->id - 1] = nullptr;
                findNextLiveBid();
            }
            if (ask->qty == 0) {
                asks_[best_ask_].pop();
                order_map_[ask->id - 1] = nullptr;
                findNextLiveAsk();
            }
        }
        return trades;
    }

    void reset() {
        for (auto& l : bids_) l.reset();
        for (auto& l : asks_) l.reset();
        order_map_.clear();
        next_id_  = 1;
        best_bid_ = -1;
        best_ask_ = N_LEVELS;
    }

    [[nodiscard]] size_t bidLevels() const {
        size_t c = 0; for (auto& l : bids_) if (!l.empty()) ++c; return c;
    }
    [[nodiscard]] size_t askLevels() const {
        size_t c = 0; for (auto& l : asks_) if (!l.empty()) ++c; return c;
    }
    [[nodiscard]] size_t openOrders() const noexcept {
        size_t c = 0;
        for (auto* o : order_map_) if (o) ++c;
        return c;
    }
};

// ─── Helpers ──────────────────────────────────────────────────────────
[[gnu::always_inline, gnu::const]]
constexpr Price toTicks(double p) noexcept {
    return static_cast<Price>(p * 100.0 + 0.5);
}

// ─── main ─────────────────────────────────────────────────────────────
int main(int argc, char* argv[]) {
    const char* csv_file = "data/market_data.csv";
    if (argc > 1) csv_file = argv[1];

    std::ifstream f(csv_file);
    if (!f.is_open()) {
        std::cerr << "Cannot open " << csv_file << "\n";
        return 1;
    }

    std::vector<Price>     prices;
    std::vector<Quantity>  qtys;
    std::vector<Side>      sides;
    std::vector<Timestamp> timestamps;

    std::string line, ts_str, type_str, side_str, price_str, qty_str;
    // skip header
    std::getline(f, line);

    while (std::getline(f, line)) {
        std::stringstream ss(line);
        std::getline(ss, ts_str,   ',');
        std::getline(ss, type_str, ',');
        std::getline(ss, side_str, ',');
        std::getline(ss, price_str,',');
        std::getline(ss, qty_str,  ',');

        // Only process Add events (ignore Cancel and Trade)
        if (type_str != "A") continue;

        Timestamp ts = std::stoull(ts_str);
        // Convert price from dollars to ticks (*100) and round to integer
        Price p = static_cast<Price>(std::stod(price_str) * 100.0 + 0.5);
        if (p < TICK_MIN) p = TICK_MIN;
        if (p > TICK_MAX) p = TICK_MIN + (p % (TICK_MAX - TICK_MIN + 1));
        // Convert quantity from BTC to satoshis (*1e8)
        Quantity q = static_cast<Quantity>(std::stod(qty_str) * 1e8);
        Side s = (side_str == "B") ? Side::Buy : Side::Sell;

        prices.push_back(p);
        qtys.push_back(q);
        sides.push_back(s);
        timestamps.push_back(ts);
    }
    f.close();

    size_t N = prices.size();
    if (N == 0) {
        std::cerr << "No valid Add events in file.\n";
        return 1;
    }
    std::cout << "Loaded " << N << " Add events from " << csv_file << "\n";

    // Instantiate engine – v7 uses a MemoryPool
    size_t pool_size = N * sizeof(Order) * 2 + 1024;
    MemoryPool pool(pool_size);
    OrderBook* book = new OrderBook(pool);
    book->prepare(N);

    auto t0 = std::chrono::high_resolution_clock::now();
    for (size_t i = 0; i < N; ++i)
        book->addOrder(prices[i], qtys[i], sides[i], timestamps[i]);
    auto trades = book->match();
    auto t1 = std::chrono::high_resolution_clock::now();

    auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
    double avg_ns = static_cast<double>(ns) / N;
    uint64_t throughput = static_cast<uint64_t>(1e9 * N / ns);

    std::cout << "═══════════════════════════════════════\n";
    std::cout << " v2 — Real Data Replay (Binance)\n";
    std::cout << "═══════════════════════════════════════\n";
    std::cout << " Add events   : " << N          << '\n';
    std::cout << " Trades       : " << trades.size() << '\n';
    std::cout << " Total ns     : " << ns         << '\n';
    std::cout << " Avg ns/order : " << avg_ns     << '\n';
    std::cout << " Throughput   : " << throughput  << " orders/sec\n";
    std::cout << "═══════════════════════════════════════\n";

    delete book;
    return 0;
}