#include <iostream>
#include <vector>
#include <algorithm>
#include <chrono>
#include <random>
#include <cstdint>
#include <memory_resource>
#include <stdexcept>

using OrderId = uint64_t;
using Quantity = uint64_t;
using Price = double;
using Timestamp = uint64_t;

enum class Side { Buy, Sell };

struct Order {
    OrderId   id;
    Price     price;
    Quantity  qty;
    Side      side;
    Timestamp timestamp;

    Order(OrderId id, Price price, Quantity qty, Side side, Timestamp ts)
        : id(id), price(price), qty(qty), side(side), timestamp(ts) {}

    Order(Order&&) noexcept = default;
    Order& operator=(Order&&) noexcept = default;

    Order(const Order&) = delete;
    Order& operator=(const Order&) = delete;
};

struct Trade {
    OrderId  buy_id;
    OrderId  sell_id;
    Price    price;
    Quantity qty;
};

class MemoryPool {
    static constexpr size_t BUFFER_SIZE = 1 << 20;  // 1 MiB
    char buffer_[BUFFER_SIZE];
    std::pmr::monotonic_buffer_resource resource_{buffer_, BUFFER_SIZE};

public:
    template<typename T, typename... Args>
    T* allocate(Args&&... args) {
        void* mem = resource_.allocate(sizeof(T), alignof(T));
        return new (mem) T(std::forward<Args>(args)...);
    }

    void deallocate(void*, size_t) { /* no-op */ }
};

class OrderBook {
    MemoryPool& pool_;
    std::vector<Order*> bids_;
    std::vector<Order*> asks_;

    static bool bidCompare(const Order* a, const Order* b) {
        if (a->price != b->price) return a->price > b->price;
        return a->timestamp < b->timestamp;
    }
    static bool askCompare(const Order* a, const Order* b) {
        if (a->price != b->price) return a->price < b->price;
        return a->timestamp < b->timestamp;
    }

public:
    explicit OrderBook(MemoryPool& pool) : pool_(pool) {}

    OrderId addOrder(Price price, Quantity qty, Side side, Timestamp ts) {
        static OrderId nextId = 1;
        OrderId id = nextId++;
        Order* order = pool_.allocate<Order>(id, price, qty, side, ts);
        if (side == Side::Buy)
            bids_.push_back(order);
        else
            asks_.push_back(order);
        return id;
    }

    std::vector<Trade> match() {
        std::sort(bids_.begin(), bids_.end(), bidCompare);
        std::sort(asks_.begin(), asks_.end(), askCompare);

        std::vector<Trade> trades;
        while (!bids_.empty() && !asks_.empty()) {
            Order* bestBid = bids_.front();
            Order* bestAsk = asks_.front();

            if (bestBid->price < bestAsk->price) break;

            Quantity matchQty = std::min(bestBid->qty, bestAsk->qty);
            trades.push_back({bestBid->id, bestAsk->id, bestAsk->price, matchQty});

            bestBid->qty -= matchQty;
            bestAsk->qty -= matchQty;

            if (bestBid->qty == 0) bids_.erase(bids_.begin());
            if (bestAsk->qty == 0) asks_.erase(asks_.begin());
        }
        return trades;
    }

    void reset() {
        bids_.clear();
        asks_.clear();
    }
};

int main() {
    MemoryPool pool;
    OrderBook book(pool);

    std::mt19937 rng(42);
    std::uniform_real_distribution<double> priceDist(95.0, 105.0);
    std::uniform_int_distribution<Quantity> qtyDist(1, 100);
    std::uniform_int_distribution<int> sideDist(0, 1);

    constexpr size_t NUM_ORDERS = 100'000;
    Timestamp ts = 0;

    for (size_t i = 0; i < 10'000; ++i) {
        book.addOrder(priceDist(rng), qtyDist(rng),
                      sideDist(rng) ? Side::Buy : Side::Sell, ts++);
    }
    book.reset();

    auto start = std::chrono::high_resolution_clock::now();

    for (size_t i = 0; i < NUM_ORDERS; ++i) {
        book.addOrder(priceDist(rng), qtyDist(rng),
                      sideDist(rng) ? Side::Buy : Side::Sell, ts++);
    }
    auto trades = book.match();

    auto end = std::chrono::high_resolution_clock::now();
    auto totalNs = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();

    std::cout << "Processed " << NUM_ORDERS << " orders, "
              << trades.size() << " trades.\n";
    std::cout << "Total time: " << totalNs << " ns\n";
    std::cout << "Avg per order: " << static_cast<double>(totalNs) / NUM_ORDERS << " ns\n";
    return 0;
}
