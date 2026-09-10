#include "EventGenerator.hpp"

#include <cmath>

EventGenerator::EventGenerator(double initial_fair_value, double fair_value_vol, double tick_size,
                                double limit_order_rate_per_step, double market_order_rate_per_step,
                                double avg_order_size, unsigned seed)
    : fair_value_(initial_fair_value),
      fair_value_vol_(fair_value_vol),
      tick_size_(tick_size),
      limit_order_rate_(limit_order_rate_per_step),
      market_order_rate_(market_order_rate_per_step),
      avg_order_size_(avg_order_size),
      rng_(seed),
      limit_arrivals_(limit_order_rate_per_step),
      market_arrivals_(market_order_rate_per_step),
      size_dist_(1.0 / std::max(avg_order_size, 1.0)),
      offset_dist_(1.0 / 4.0) {}  // mean ~4 ticks away from fair value

Price EventGenerator::to_ticks(double price) const {
    return static_cast<Price>(std::llround(price / tick_size_));
}

void EventGenerator::step(MatchingEngine &engine) {
    fair_value_ += fair_value_vol_ * fv_noise_(rng_);
    fair_value_ = std::max(fair_value_, tick_size_ * 10);  // keep it away from zero

    // Occasionally cancel a random stale background order, so the book
    // doesn't grow without bound over a long run.
    if (!outstanding_background_orders_.empty() && cancel_dist_(rng_)) {
        size_t idx = rng_() % outstanding_background_orders_.size();
        OrderId id = outstanding_background_orders_[idx];
        engine.cancel(id);
        outstanding_background_orders_.erase(outstanding_background_orders_.begin() +
                                               static_cast<long>(idx));
    }

    int n_limits = limit_arrivals_(rng_);
    for (int i = 0; i < n_limits; ++i) {
        Side side = side_dist_(rng_) ? Side::Buy : Side::Sell;
        double offset_ticks = offset_dist_(rng_);
        double price = side == Side::Buy ? fair_value_ - offset_ticks * tick_size_
                                          : fair_value_ + offset_ticks * tick_size_;
        Quantity qty = std::max<Quantity>(1, static_cast<Quantity>(std::round(size_dist_(rng_))));

        OrderId id = engine.submit_limit(side, to_ticks(price), qty, /*is_market_maker=*/false);
        outstanding_background_orders_.push_back(id);
        if (outstanding_background_orders_.size() > 2000) {
            outstanding_background_orders_.pop_front();
        }
    }

    int n_markets = market_arrivals_(rng_);
    for (int i = 0; i < n_markets; ++i) {
        Side side = side_dist_(rng_) ? Side::Buy : Side::Sell;
        Quantity qty = std::max<Quantity>(1, static_cast<Quantity>(std::round(size_dist_(rng_))));
        engine.submit_market(side, qty, /*is_market_maker=*/false);
    }
}
