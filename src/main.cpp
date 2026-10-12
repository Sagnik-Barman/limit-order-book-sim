#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <vector>

#include "EventGenerator.hpp"
#include "MarketMaker.hpp"
#include "MatchingEngine.hpp"

struct SimConfig {
    double tick_size = 0.01;
    double initial_fair_value = 100.0;
    double fair_value_vol = 0.015;      // per-step std dev of the fair value random walk
    double limit_order_rate = 3.0;      // background limit orders per step (Poisson mean)
    double market_order_rate = 0.6;     // background market orders per step (Poisson mean)
    double avg_order_size = 8.0;
    Price half_spread_ticks = 15;
    Quantity quote_size = 10;
    double inventory_skew_ticks_per_unit = 0.015;
    Quantity max_inventory = 300;
    long n_steps = 20000;
    unsigned seed = 42;
};

int main(int argc, char **argv) {
    SimConfig cfg;
    if (argc > 1) cfg.n_steps = std::stol(argv[1]);

    MatchingEngine engine;
    EventGenerator gen(cfg.initial_fair_value, cfg.fair_value_vol, cfg.tick_size,
                        cfg.limit_order_rate, cfg.market_order_rate, cfg.avg_order_size, cfg.seed);
    MarketMaker mm(cfg.tick_size, cfg.half_spread_ticks, cfg.quote_size,
                    cfg.inventory_skew_ticks_per_unit, cfg.max_inventory);

    // seed the book with an initial two-sided quote so the very first
    // background market orders have something to trade against
    mm.update_quotes(engine, cfg.initial_fair_value);
    engine.pop_new_fills();  // discard (nothing should have crossed on an empty book)

    std::ofstream log("output/simulation_log.csv");
    log << "step,fair_value,mid_price,best_bid,best_ask,spread_dollars,mm_inventory,mm_cash,mm_mtm_pnl\n";

    std::vector<double> mtm_history;
    Quantity max_abs_inventory = 0;
    long n_mm_fills = 0;

    for (long t = 0; t < cfg.n_steps; ++t) {
        gen.step(engine);

        auto fills = engine.pop_new_fills();
        for (const auto &f : fills) {
            if (f.resting_is_mm || f.aggressor_is_mm) ++n_mm_fills;
        }
        mm.process_fills(fills);

        auto mid_ticks = engine.book().mid_price_ticks();
        double ref = mid_ticks ? (*mid_ticks * cfg.tick_size) : gen.fair_value();
        mm.update_quotes(engine, ref);
        engine.advance_time();

        max_abs_inventory = std::max(max_abs_inventory, static_cast<Quantity>(std::abs(mm.inventory())));
        double mtm = mm.mark_to_market_pnl(ref);
        mtm_history.push_back(mtm);

        if (t % 10 == 0 || t == cfg.n_steps - 1) {
            auto bb = engine.book().best_bid();
            auto ba = engine.book().best_ask();
            auto spr = engine.book().spread_ticks();
            auto fmt = [&](std::optional<Price> p) {
                return p ? std::to_string(static_cast<double>(*p) * cfg.tick_size) : std::string();
            };
            log << t << "," << gen.fair_value() << "," << ref << "," << fmt(bb) << "," << fmt(ba) << ","
                << fmt(spr) << "," << mm.inventory() << "," << mm.realized_cash() << "," << mtm << "\n";
        }
    }
    log.close();

    // --- summary stats ---
    double final_pnl = mtm_history.empty() ? 0.0 : mtm_history.back();
    double running_max = -1e18, max_drawdown = 0.0;
    for (double v : mtm_history) {
        running_max = std::max(running_max, v);
        max_drawdown = std::min(max_drawdown, v - running_max);
    }

    std::vector<double> step_pnl_changes;
    for (size_t i = 1; i < mtm_history.size(); ++i) {
        step_pnl_changes.push_back(mtm_history[i] - mtm_history[i - 1]);
    }
    double mean_change = 0.0, sq_sum = 0.0;
    for (double v : step_pnl_changes) mean_change += v;
    mean_change /= std::max<size_t>(step_pnl_changes.size(), 1);
    for (double v : step_pnl_changes) sq_sum += (v - mean_change) * (v - mean_change);
    double std_change = std::sqrt(sq_sum / std::max<size_t>(step_pnl_changes.size(), 1));
    double sharpe_like = std_change > 0 ? mean_change / std_change * std::sqrt(252.0) : 0.0;

    std::cout << "=== Simulation complete: " << cfg.n_steps << " steps ===\n";
    std::cout << "Final mark-to-market PnL:   " << final_pnl << "\n";
    std::cout << "Final realized cash:        " << mm.realized_cash() << "\n";
    std::cout << "Final inventory:            " << mm.inventory() << " shares\n";
    std::cout << "Max |inventory| reached:    " << max_abs_inventory << " shares (cap was "
              << cfg.max_inventory << ")\n";
    std::cout << "Max drawdown (PnL units):   " << max_drawdown << "\n";
    std::cout << "Step-PnL Sharpe-like ratio: " << sharpe_like
              << "  (illustrative only -- 'per step' isn't 'per day', see README)\n";
    std::cout << "MM fills:                   " << n_mm_fills << "\n";
    std::cout << "Background limit orders/step (mean): " << cfg.limit_order_rate << "\n";
    std::cout << "Log written to output/simulation_log.csv\n";

    return 0;
}
