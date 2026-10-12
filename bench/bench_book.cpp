// bench/bench_book.cpp
// --------------------
// Throughput and per-operation latency of OrderBook on its own: no strategy,
// no event generator, no logging.
//
//   book_bench [n_ops] [seed]        (defaults: 1,000,000 ops, seed 7)
//
// A seeded stream of operations is generated up front (so random-number
// generation is not timed), then replayed into the book twice:
//   1. untimed per op, to measure total throughput (operations per second);
//   2. with a steady_clock read around every operation, for p50/p99/p99.9
//      latency per operation type. The cost of one clock read pair is
//      measured and printed, so it can be subtracted.
//
// Mix: 60% limit orders (prices within +-20 ticks of a drifting mid, so some
// cross and fill), 30% cancels of a random live order, 10% market orders.
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

#include "../include/OrderBook.hpp"

namespace {

using Clock = std::chrono::steady_clock;

enum class OpType : std::uint8_t { Limit, Cancel, Market };

struct Op {
    OpType type;
    Side side;
    Price price;
    Quantity qty;
    std::uint64_t cancel_pick;  // used to choose which live order a cancel targets
};

std::vector<Op> make_ops(std::size_t n, unsigned seed) {
    std::mt19937_64 rng(seed);
    std::uniform_real_distribution<double> u(0.0, 1.0);
    std::uniform_int_distribution<int> offset(-20, 20);
    std::uniform_int_distribution<int> qty(1, 100);
    std::vector<Op> ops(n);
    double mid = 10000.0;
    for (auto &op : ops) {
        mid += (u(rng) - 0.5) * 0.5;  // slow random drift, in ticks
        double r = u(rng);
        op.type = r < 0.6 ? OpType::Limit : (r < 0.9 ? OpType::Cancel : OpType::Market);
        op.side = u(rng) < 0.5 ? Side::Buy : Side::Sell;
        // buys rest below the mid and sells above it on average, with overlap so some cross
        int off = offset(rng);
        op.price = static_cast<Price>(mid) + (op.side == Side::Buy ? -std::abs(off) + 3 : std::abs(off) - 3);
        op.qty = qty(rng);
        op.cancel_pick = rng();
    }
    return ops;
}

// Live resting order ids, with O(1) random removal (swap with last).
struct LiveSet {
    std::vector<OrderId> ids;
    std::unordered_map<OrderId, std::size_t> pos;
    void add(OrderId id) { pos[id] = ids.size(); ids.push_back(id); }
    void remove(OrderId id) {
        auto it = pos.find(id);
        if (it == pos.end()) return;
        std::size_t i = it->second;
        ids[i] = ids.back();
        pos[ids[i]] = i;
        ids.pop_back();
        pos.erase(it);
    }
};

struct Runner {
    OrderBook book;
    LiveSet live;
    OrderId next_id = 1;
    Timestamp ts = 0;

    // Returns the op type actually executed (a cancel with nothing live becomes a no-op limit).
    OpType apply(const Op &op) {
        ++ts;
        if (op.type == OpType::Cancel && !live.ids.empty()) {
            OrderId id = live.ids[op.cancel_pick % live.ids.size()];
            book.cancel(id);
            live.remove(id);
            return OpType::Cancel;
        }
        Order o{next_id++, op.side, OpType::Market == op.type ? OrderType::Market : OrderType::Limit,
                op.price, op.qty, ts, false};
        std::vector<Fill> fills = (op.type == OpType::Market) ? book.submit_market(o) : book.submit_limit(o);
        for (const auto &f : fills)
            if (!book.contains(f.resting_id)) live.remove(f.resting_id);  // fully filled
        if (op.type != OpType::Market && book.contains(o.id)) live.add(o.id);  // remainder rested
        return op.type == OpType::Market ? OpType::Market : OpType::Limit;
    }
};

double pct(std::vector<std::uint32_t> &v, double p) {
    if (v.empty()) return 0.0;
    std::size_t k = static_cast<std::size_t>(p * (v.size() - 1));
    std::nth_element(v.begin(), v.begin() + static_cast<long>(k), v.end());
    return v[k];
}

}  // namespace

int main(int argc, char **argv) {
    std::size_t n = argc > 1 ? std::stoull(argv[1]) : 1'000'000;
    unsigned seed = argc > 2 ? static_cast<unsigned>(std::stoul(argv[2])) : 7;
    const auto ops = make_ops(n, seed);

    // 1. throughput
    {
        Runner r;
        auto t0 = Clock::now();
        for (const auto &op : ops) r.apply(op);
        double s = std::chrono::duration<double>(Clock::now() - t0).count();
        std::cout << "Operations:          " << n << "\n";
        std::cout << std::fixed << std::setprecision(0);
        std::cout << "Throughput:          " << n / s << " ops/s (" << std::setprecision(1) << s * 1e9 / n
                  << " ns/op average, incl. the benchmark's own live-order bookkeeping)\n";
        std::cout << "Resting at end:      " << r.book.bid_order_count() + r.book.ask_order_count() << " orders\n";
    }

    // clock overhead: cost of one back-to-back pair of clock reads
    std::vector<std::uint32_t> overhead;
    overhead.reserve(100000);
    for (int i = 0; i < 100000; ++i) {
        auto a = Clock::now();
        auto b = Clock::now();
        overhead.push_back(static_cast<std::uint32_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(b - a).count()));
    }

    // 2. per-operation latency
    Runner r;
    std::vector<std::uint32_t> lat[3];
    for (auto &v : lat) v.reserve(n);
    for (const auto &op : ops) {
        auto a = Clock::now();
        OpType t = r.apply(op);
        auto b = Clock::now();
        lat[static_cast<int>(t)].push_back(
            static_cast<std::uint32_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(b - a).count()));
    }
    const char *names[3] = {"limit", "cancel", "market"};
    std::cout << std::setprecision(0);
    std::cout << "\nLatency per operation (ns; includes the clock overhead below and the benchmark's own live-order bookkeeping)\n";
    std::cout << "  type        count      p50      p99    p99.9\n";
    for (int t = 0; t < 3; ++t) {
        auto &v = lat[t];
        std::cout << "  " << std::left << std::setw(8) << names[t] << std::right << std::setw(10) << v.size()
                  << std::setw(9) << pct(v, 0.50) << std::setw(9) << pct(v, 0.99) << std::setw(9) << pct(v, 0.999)
                  << "\n";
    }
    std::cout << "Clock-read overhead: p50 " << pct(overhead, 0.5) << " ns\n";
    return 0;
}
