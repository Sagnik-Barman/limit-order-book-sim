#pragma once
#include <list>
#include <map>
#include <optional>
#include <unordered_map>
#include <vector>

#include "Order.hpp"

// A single-instrument limit order book with strict price-time priority
// (FIFO within a price level -- the standard convention on real exchanges
// for the default order type). Bids are kept in descending price order,
// asks in ascending order, so `begin()` on either map is always the best
// price on that side.
//
// Matching lives here rather than in a separate class: on real exchanges
// the book *is* the matching engine's core data structure, and splitting
// them apart is mostly artificial. `MatchingEngine` (see MatchingEngine.hpp)
// is a thin layer on top that owns order-id/timestamp generation and the
// trade tape -- the part that's genuinely a separate concern.
class OrderBook {
public:
    // Attempts to match `order` against the resting book immediately;
    // any unfilled remainder rests on the book. Returns the fills that
    // occurred (empty if nothing crossed).
    std::vector<Fill> submit_limit(Order order);

    // Market orders never rest -- they take liquidity until filled or the
    // book on that side is empty. Returns the fills that occurred.
    std::vector<Fill> submit_market(Order order);

    // Removes a resting order. Returns false if the id wasn't found
    // (already filled or already cancelled) -- callers should treat that
    // as a normal, expected outcome, not an error.
    bool cancel(OrderId id);

    std::optional<Price> best_bid() const;
    std::optional<Price> best_ask() const;
    Quantity quantity_at_bid(Price p) const;
    Quantity quantity_at_ask(Price p) const;

    // NOTE: returns the mid *in ticks* (average of best_bid/best_ask, both
    // integer tick prices), not a dollar price -- OrderBook is
    // deliberately unit-agnostic and doesn't know the tick size. Callers
    // that need dollars must multiply by their own tick_size (see main.cpp).
    std::optional<double> mid_price_ticks() const;
    std::optional<Price> spread_ticks() const;

    // Top `depth` price levels on each side, for logging/plotting: pairs of
    // (price, total quantity resting at that price).
    std::vector<std::pair<Price, Quantity>> bid_levels(size_t depth) const;
    std::vector<std::pair<Price, Quantity>> ask_levels(size_t depth) const;

    size_t bid_order_count() const { return bids_by_id_.size(); }
    size_t ask_order_count() const { return asks_by_id_.size(); }

private:
    using Level = std::list<Order>;

    std::map<Price, Level, std::greater<Price>> bids_;  // best bid = bids_.begin()
    std::map<Price, Level, std::less<Price>> asks_;     // best ask = asks_.begin()

    // For O(1) cancel: order id -> (price level, iterator into that level's list)
    std::unordered_map<OrderId, std::pair<Price, Level::iterator>> bids_by_id_;
    std::unordered_map<OrderId, std::pair<Price, Level::iterator>> asks_by_id_;

    std::vector<Fill> match(Order &incoming);
    void rest(Order order);
    void erase_from_index(OrderId id, Side side);
};
