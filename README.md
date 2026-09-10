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
implementation would hit in the matching logic. Cancel is O(1) via an
`unordered_map<OrderId, list-iterator>` index kept in sync with the book.

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

`lob_sim` writes `output/simulation_log.csv` (price, spread, inventory,
PnL sampled every 10 steps) and prints a summary: final mark-to-market
PnL, max inventory reached, max drawdown, and fill count.
`scripts/plot_results.py` turns that into `output/simulation_summary.png`
— price action vs. the latent fair value, inventory over time, and PnL.

## Reading the output

The market maker should show: inventory oscillating around zero (not
drifting to one side and staying there — if it does, the skew parameter
is too weak), a positive mark-to-market PnL trending up on average (it's
capturing the bid-ask spread from uninformed flow), and drawdowns that
correspond to periods where the fair value moved fast enough that the
maker got run over on one side before it could re-quote.

## What to be ready to explain in an interview

- **Why price-time priority, and why FIFO within a level matters**: it's
  what makes being first at a price level valuable — the entire concept
  of "queue position" in real market making comes from this rule.
- **Why the market maker skews quotes by inventory** rather than just
  quoting a fixed spread: an un-skewed maker with no inventory limit will
  eventually take on unbounded directional risk if the fair value trends,
  because it keeps getting run over on the same side. Skewing quotes is
  the mechanism that makes inventory mean-revert toward zero.
- **The gap between this and a real market maker**: no adverse selection
  modeling (this repo's background traders are not informed — a real
  market has to worry about who it's trading against), no latency/queue
  position modeling, spread and skew are fixed constants rather than
  derived from a risk-aversion parameter and time horizon the way
  Avellaneda-Stoikov actually derives them.

## Known limitations (say these before you're asked)

- The inventory cap is checked before quoting, not sized into the quote
  itself — so a single large fill can push inventory a bit past the cap
  before the next requote catches it (visible in the summary output: max
  inventory sometimes slightly exceeds the configured cap). A production
  version would size the resting quote down as inventory approaches the
  limit instead of a hard on/off cutoff.
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
  watch the market maker's PnL degrade — then fix it by widening spreads
  when recent volatility spikes.
- Multi-level quoting (resting size at more than one price level per
  side) instead of a single quote per side.
- Latency: add a fixed or random delay between the market maker deciding
  to requote and the order actually reaching the book.
