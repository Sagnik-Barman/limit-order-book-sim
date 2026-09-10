#pragma once
#include <optional>
#include <vector>

#include "MatchingEngine.hpp"

// A minimal but genuine market-making strategy: quote a two-sided market
// around a reference price with a fixed half-spread, re-quoting every
// tick, and skew the quotes based on current inventory so the strategy
// naturally leans against its own position (buy less / sell more eagerly
// when long, and vice versa) rather than accumulating unbounded risk.
//
// This is the textbook "inventory-aware" market maker (a simplified,
// discrete-time cousin of Avellaneda-Stoikov, 2008) -- not because it's
// the most sophisticated possible strategy, but because it's the smallest
// version that actually demonstrates the core idea a quant interview will
// ask about: a market maker's job is managing inventory risk while
// capturing the spread, not directional prediction.
class MarketMaker {
public:
    MarketMaker(double tick_size, Price half_spread_ticks, Quantity quote_size,
                double inventory_skew_ticks_per_unit, Quantity max_inventory);

    // Cancels this maker's previous quotes and submits fresh bid/ask
    // limit orders around `reference_price`, skewed by current inventory.
    void update_quotes(MatchingEngine &engine, double reference_price);

    // Call with engine.pop_new_fills() each tick to keep inventory/cash
    // up to date.
    void process_fills(const std::vector<Fill> &fills);

    double realized_cash() const { return cash_; }
    double mark_to_market_pnl(double reference_price) const;
    Quantity inventory() const { return inventory_; }

private:
    double tick_size_;
    Price half_spread_ticks_;
    Quantity quote_size_;
    double inventory_skew_ticks_per_unit_;
    Quantity max_inventory_;

    double cash_ = 0.0;
    Quantity inventory_ = 0;

    std::optional<OrderId> resting_bid_id_;
    std::optional<OrderId> resting_ask_id_;

    Price to_ticks(double price) const;
};
