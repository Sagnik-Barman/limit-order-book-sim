#pragma once
#include <vector>

#include "Order.hpp"
#include "OrderBook.hpp"

// Owns order-id / timestamp generation and the trade tape (the append-only
// log of every fill that has ever happened) on top of the raw OrderBook.
// This is the object every external actor (MarketMaker, EventGenerator,
// main's simulation loop) actually talks to.
class MatchingEngine {
public:
    OrderId submit_limit(Side side, Price price, Quantity qty, bool is_market_maker);
    OrderId submit_market(Side side, Quantity qty, bool is_market_maker);
    bool cancel(OrderId id);

    void advance_time() { ++clock_; }
    Timestamp now() const { return clock_; }

    const OrderBook &book() const { return book_; }
    const std::vector<Fill> &trade_tape() const { return trade_tape_; }

    // Fills since the last time this was called -- lets callers (e.g. the
    // market maker, or a PnL logger) process only what's new each tick
    // without rescanning the whole tape.
    std::vector<Fill> pop_new_fills();

private:
    OrderBook book_;
    std::vector<Fill> trade_tape_;
    size_t last_popped_index_ = 0;
    OrderId next_id_ = 1;
    Timestamp clock_ = 0;
};
