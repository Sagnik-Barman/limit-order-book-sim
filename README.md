# Limit Order Book + Market Making Simulator (C++)

A single-instrument limit order book with strict price-time priority, a
matching engine on top of it, synthetic "noise trader" order flow, and an
inventory-aware market-making strategy that trades against that flow.

This is the project that pairs your C++ background with the quant-dev
(as opposed to quant-researcher) side of trading firms — it demonstrates
you can build the actual infrastructure a strategy runs on, not just
backtest a signal in Python.

## What's actually being modeled

**Order book (`OrderBook.hpp/cpp`).** Bids and asks are kept in
`std::map<Price, std::list<Order>>` (descending / ascending), so the best
price on either side is always `begin()`. Within a price level, orders are
FIFO — first in, first filled — which is strict **price-time priority**,
the standard matching rule at most real exchanges. Prices are integer
ticks, not doubles — real exchanges represent price this way, and it
sidesteps an entire category of floating-point equality bugs a naive
implementation would hit in the matching logic. Cancel is O(1): an
`unordered_map<OrderId, (level iterator, list iterator)>` index stores, for
every resting order, an iterator to its price level in the `std::map` and to
its node in that level's list, so a cancel never searches the map. (`std::map`
iterators stay valid until that level is erased, and a level is only erased
once its last order is gone.) Adding an order at a brand-new price is still
O(log L) for the map insert.

**Matching engine (`MatchingEngine.hpp/cpp`).** A thin layer over the book
that owns order-id/timestamp generation and the trade tape (append-only
fill log) — the part of "matching" that's genuinely separate from the
book's own data structure.

**Background order flow (`EventGenerator.hpp/cpp`).** A latent "fair
value" follows a random walk; limit orders arrive as a Poisson process at
random offsets around it, occasional market orders arrive and actually
cross the book. This is what gives the market maker something realistic
to trade against — deliberately simple (no informed traders, no flow
clustering), because the point of this repo is the engine and the
strategy, not a research-grade microstructure model.

**Market maker (`MarketMaker.hpp/cpp`).** Quotes a two-sided market every
tick: bid and ask at a fixed half-spread around the current book mid,
**skewed by inventory** — long a position pushes both quotes down (less
eager to buy more, more eager to sell), short pushes them up, and a hard
cap stops it from quoting the side that would grow an already-large
position further. This is the simplified, discrete-time cousin of
**Avellaneda-Stoikov (2008)**, the standard reference model for
inventory-aware market making — worth reading the original paper if you
want to extend this with the full stochastic-control derivation (optimal
skew as a function of risk aversion, time-to-horizon, and inventory, not
just a fixed linear skew like this repo uses).

## Building and running

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make
cd ..
./build/lob_sim 20000        # run 20,000 simulation steps
python scripts/plot_results.py   # needs pandas + matplotlib
```

### Order-book benchmark

```bash
./build/book_bench 1000000     # 1M operations: 60% limit adds, 30% cancels, 10% market orders
```

Replays a pre-generated, seeded stream of order operations straight into
`OrderBook` (no strategy, no logging) and prints throughput (operations per
second) and per-operation latency percentiles (p50 / p99 / p99.9) for each
operation type. Run it on your own machine, in a Release build, with nothing
else heavy running; per-operation timing adds roughly 20-40 ns of clock
overhead, which the output reports so you can subtract it.

`lob_sim` writes `output/simulation_log.csv` (price, spread, inventory,
PnL sampled every 10 steps) and prints a summary: final mark-to-market
PnL, max inventory reached, max drawdown, and fill count.
`scripts/plot_results.py` turns that into `output/simulation_summary.png`
— price action vs. the latent fair value, inventory over time, and PnL.

## Reading the output

The market maker should show inventory oscillating around zero (if it drifts
to one side and stays there, the skew is too weak) and drawdowns in periods
where the fair value moved fast enough to run the maker over before it could
re-quote. A single run proves little: see `ablation/ABLATION_RESULTS.md` for
the 30-seed results, where a tight fixed spread loses money on average because
55% of its fills are adverse, and an EWMA-volatility-scaled spread fixes it.

## What to be ready to explain in an interview

- **Why price-time priority, and why FIFO within a level matters**: it's
  what makes being first at a price level valuable — the entire concept
  of "queue position" in real market making comes from this rule.
- **Why the market maker skews quotes by inventory** rather than just
  quoting a fixed spread: an un-skewed maker with no inventory limit will
  eventually take on unbounded directional risk if the fair value trends,
  because it keeps getting run over on the same side. Skewing quotes is
  the mechanism that makes inventory mean-revert toward zero.
- **The gap between this and a real market maker**: adverse selection here
  comes only from background flow priced off the true value hitting quotes
  centred on a lagging book mid (there are no explicitly informed traders);
  no latency or queue-position modelling; and the spread rule is a heuristic
  (base + multiple of EWMA vol) rather than derived from a risk-aversion
  parameter and time horizon the way Avellaneda-Stoikov derives it.

## Known limitations (say these before you're asked)

- The inventory cap is enforced by sizing each quote to the room left under
  the cap (fixed Sep 2026; it used to switch a side on/off and overshot to
  309). Max |inventory| is now exactly the cap in every ablation run.
- Requoting happens every single simulation step — unrealistic vs. a real
  system with order-entry latency and a market that doesn't literally wait
  for you; also generates far more cancel/replace churn than a live
  strategy would want, though it costs nothing in this simulated,
  latency-free environment.
- The "Sharpe-like" ratio printed at the end is annualized by
  `sqrt(252)` purely as an illustrative scaling — a simulation step here
  is not a trading day, so treat that number as directional, not literal.

## Extensions worth doing if you have more time

- Replace the fixed half-spread/skew with the actual Avellaneda-Stoikov
  closed-form optimal quotes (function of inventory, volatility, risk
  aversion, and time-to-horizon).
- Add a second, informed "momentum" trader type to the event generator and
  watch the market maker's PnL degrade. (Widening spreads when recent
  volatility spikes is done: see the vol-scaling and regime results in
  `ablation/ABLATION_RESULTS.md`.)
- Multi-level quoting (resting size at more than one price level per
  side) instead of a single quote per side.
- Latency: add a fixed or random delay between the market maker deciding
  to requote and the order actually reaching the book.
