#pragma once
#include <cstdint>
#include <string>

// Prices are kept as integer ticks (e.g. 1 tick = $0.01) rather than
// doubles. This is deliberate and matches how real exchanges represent
// prices internally: floating point equality/ordering bugs in a matching
// engine are a classic, embarrassing source of incorrect fills.
using Price = int64_t;
using Quantity = int64_t;
using OrderId = uint64_t;
using Timestamp = uint64_t;  // simulation tick counter

enum class Side { Buy, Sell };

inline Side opposite(Side s) { return s == Side::Buy ? Side::Sell : Side::Buy; }

enum class OrderType { Limit, Market };

struct Order {
    OrderId id;
    Side side;
    OrderType type;
    Price price;          // ignored for Market orders
    Quantity quantity;    // remaining, unfilled quantity
    Timestamp timestamp;  // used for time priority within a price level
    bool is_market_maker; // tags orders submitted by our MarketMaker strategy,
                           // so the engine/logger can attribute fills to it
};

struct Fill {
    Timestamp timestamp;
    Price price;
    Quantity quantity;
    OrderId aggressor_id;
    OrderId resting_id;
    Side aggressor_side;
    bool aggressor_is_mm;
    bool resting_is_mm;
};
