#include "MarketMaker.hpp"

#include <algorithm>
#include <cmath>

MarketMaker::MarketMaker(double tick_size, Price half_spread_ticks, Quantity quote_size,
                          double inventory_skew_ticks_per_unit, Quantity max_inventory)
    : tick_size_(tick_size),
      half_spread_ticks_(half_spread_ticks),
      quote_size_(quote_size),
      inventory_skew_ticks_per_unit_(inventory_skew_ticks_per_unit),
      max_inventory_(max_inventory) {}

Price MarketMaker::to_ticks(double price) const {
    return static_cast<Price>(std::llround(price / tick_size_));
}

void MarketMaker::update_quotes(MatchingEngine &engine, double reference_price) {
    if (resting_bid_id_) {
        engine.cancel(*resting_bid_id_);
        resting_bid_id_.reset();
    }
    if (resting_ask_id_) {
        engine.cancel(*resting_ask_id_);
        resting_ask_id_.reset();
    }

    // Skew: positive inventory (long) shifts BOTH quotes down, making the
    // bid less attractive to hit (we don't want to buy more) and the ask
    // more attractive to hit (we want to sell down our position). This is
    // the one-line version of what Avellaneda-Stoikov derives rigorously
    // from a risk-aversion utility function.
    double skew_ticks = inventory_ * inventory_skew_ticks_per_unit_;
    Price mid_ticks = to_ticks(reference_price);

    // Update the EWMA volatility estimate from the change in the reference price.
    double ref_ticks = reference_price / tick_size_;
    if (last_ref_ticks_ >= 0.0) {
        double change = ref_ticks - last_ref_ticks_;
        ewma_var_ticks_ = (1.0 - ewma_alpha_) * ewma_var_ticks_ + ewma_alpha_ * change * change;
    }
    last_ref_ticks_ = ref_ticks;
    Price half_spread = half_spread_ticks_ +
                        static_cast<Price>(std::llround(vol_mult_ * std::sqrt(ewma_var_ticks_)));

    Price bid_price = mid_ticks - half_spread - static_cast<Price>(std::llround(skew_ticks));
    Price ask_price = mid_ticks + half_spread - static_cast<Price>(std::llround(skew_ticks));
    // Hard inventory cap: stop adding to a position once it's large enough,
    // only quote the side that would reduce it.
    Quantity bid_size = std::min(quote_size_, max_inventory_ - inventory_);
    Quantity ask_size = std::min(quote_size_, max_inventory_ + inventory_);

    if (bid_size > 0 && bid_price > 0) {
        resting_bid_id_ = engine.submit_limit(Side::Buy, bid_price, bid_size, /*is_market_maker=*/true);
    }
    if (ask_size > 0) {
        resting_ask_id_ = engine.submit_limit(Side::Sell, ask_price, ask_size, /*is_market_maker=*/true);
    }
}

void MarketMaker::process_fills(const std::vector<Fill> &fills) {
    for (const auto &f : fills) {
        double trade_price = static_cast<double>(f.price) * tick_size_;
        double notional = trade_price * static_cast<double>(f.quantity);

        if (f.resting_is_mm) {
            // Our resting quote got hit. aggressor_side is the side of the
            // order that crossed us, so we did the opposite.
            if (f.aggressor_side == Side::Buy) {
                // someone bought from our resting ask -> we sold
                inventory_ -= f.quantity;
                cash_ += notional;
            } else {
                // someone sold into our resting bid -> we bought
                inventory_ += f.quantity;
                cash_ -= notional;
            }
        } else if (f.aggressor_is_mm) {
            // Shouldn't normally happen (we only ever rest limit orders
            // priced away from the touch), but handle it for correctness.
            if (f.aggressor_side == Side::Buy) {
                inventory_ += f.quantity;
                cash_ -= notional;
            } else {
                inventory_ -= f.quantity;
                cash_ += notional;
            }
        }
    }
}

double MarketMaker::mark_to_market_pnl(double reference_price) const {
    return cash_ + static_cast<double>(inventory_) * reference_price;
}
