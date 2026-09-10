#pragma once
#include <deque>
#include <random>

#include "MatchingEngine.hpp"

// Generates synthetic "noise trader" order flow so the book has something
// to trade against: a fair value that follows a random walk, limit orders
// arriving (Poisson process) at random offsets around that fair value, and
// occasional market orders that actually cross the book and create trades.
//
// This is deliberately simple (no informed/momentum traders, no
// order-flow clustering) -- the point of this repo is the matching engine
// and the market-making strategy on top of it, not a research-grade
// market-microstructure simulator.
class EventGenerator {
public:
    EventGenerator(double initial_fair_value, double fair_value_vol, double tick_size,
                    double limit_order_rate_per_step, double market_order_rate_per_step,
                    double avg_order_size, unsigned seed);

    // Advances the fair value by one random step and emits background
    // limit/market orders into `engine` for this tick.
    void step(MatchingEngine &engine);

    double fair_value() const { return fair_value_; }

private:
    double fair_value_;
    double fair_value_vol_;
    double tick_size_;
    double limit_order_rate_;
    double market_order_rate_;
    double avg_order_size_;

    std::mt19937 rng_;
    std::normal_distribution<double> fv_noise_{0.0, 1.0};
    std::poisson_distribution<int> limit_arrivals_;
    std::poisson_distribution<int> market_arrivals_;
    std::exponential_distribution<double> size_dist_{1.0};
    std::exponential_distribution<double> offset_dist_{1.0};  // ticks away from fair value
    std::bernoulli_distribution side_dist_{0.5};
    std::bernoulli_distribution cancel_dist_{0.02};  // per resting background order, per step

    std::deque<OrderId> outstanding_background_orders_;

    Price to_ticks(double price) const;
};
