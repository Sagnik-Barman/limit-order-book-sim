"""
plot_results.py
----------------
Reads output/simulation_log.csv (written by the C++ `lob_sim` binary) and
produces a summary chart: price action, market-maker inventory over time,
and mark-to-market PnL.

Usage (from the project root, after running the simulation):
    python scripts/plot_results.py
"""

from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import pandas as pd

ROOT = Path(__file__).parent.parent
df = pd.read_csv(ROOT / "output" / "simulation_log.csv")

fig, axes = plt.subplots(3, 1, figsize=(11, 10), sharex=True)

axes[0].plot(df["step"], df["fair_value"], label="Fair value (latent, unobserved)", lw=0.8, alpha=0.6)
axes[0].plot(df["step"], df["best_bid"], label="Best bid", lw=0.8, color="green")
axes[0].plot(df["step"], df["best_ask"], label="Best ask", lw=0.8, color="red")
axes[0].set_title("Price Action: Book Touch vs. Underlying Fair Value")
axes[0].legend(fontsize=8)
axes[0].grid(alpha=0.3)

axes[1].fill_between(df["step"], df["mm_inventory"], step="mid", alpha=0.5, color="steelblue")
axes[1].axhline(0, color="black", lw=0.6)
axes[1].set_title("Market Maker Inventory Over Time")
axes[1].grid(alpha=0.3)

axes[2].plot(df["step"], df["mm_mtm_pnl"], label="Mark-to-market PnL", lw=1.2, color="darkgreen")
axes[2].plot(df["step"], df["mm_cash"], label="Realized cash", lw=0.8, alpha=0.6, color="gray")
axes[2].set_title("Market Maker PnL")
axes[2].set_xlabel("Simulation step")
axes[2].legend(fontsize=8)
axes[2].grid(alpha=0.3)

plt.tight_layout()
out_path = ROOT / "output" / "simulation_summary.png"
plt.savefig(out_path, dpi=140)
print(f"Saved -> {out_path}")
