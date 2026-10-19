"""Extract stage-one disturbance and stationarity measures from ns-3 reception logs."""
from __future__ import annotations

import argparse
from pathlib import Path

import matplotlib.pyplot as plt
import pandas as pd
from statsmodels.tsa.stattools import adfuller, bds


def safe_statistic(function, values: pd.Series) -> float:
    try:
        return float(function(values)[1])
    except (ValueError, ZeroDivisionError):
        return float("nan")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--trace", required=True)
    parser.add_argument("--rx", required=True)
    parser.add_argument("--out", required=True)
    parser.add_argument("--window", type=float, default=0.5, help="synchronisation/statistics window")
    args = parser.parse_args()
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    trace = pd.read_csv(args.trace)
    rx = pd.read_csv(args.rx)
    if rx.empty:
        raise SystemExit("No packets were received; move RSUs or reduce path loss before statistical analysis.")

    rx["window_s"] = (rx.time_s // args.window * args.window).round(3)
    received = rx.groupby("window_s").size().rename("received_packets")
    expected_nodes = trace.groupby("time_s").vehicle_id.nunique().rename("active_vehicles")
    expected_nodes.index = (expected_nodes.index // args.window * args.window).round(3)
    expected_nodes = expected_nodes.groupby(level=0).max()
    summary = pd.concat([received, expected_nodes], axis=1).fillna(0).sort_index()
    summary["expected_packets"] = summary.active_vehicles * (args.window / 0.02)
    summary["loss_proxy"] = (1 - summary.received_packets / summary.expected_packets.replace(0, 1)).clip(0, 1)
    summary["arrival_delta"] = summary.received_packets.diff().fillna(0)
    summary["adf_pvalue"] = safe_statistic(adfuller, summary.received_packets)
    summary["bds_pvalue"] = safe_statistic(lambda x: bds(x, max_dim=2), summary.received_packets)
    summary.to_csv(out / "summary.csv", index_label="window_s")

    interarrival = rx.time_s.sort_values().diff().dropna()
    metrics = pd.DataFrame([{
        "mean_loss_proxy": summary.loss_proxy.mean(),
        "interarrival_cv": interarrival.std() / interarrival.mean() if not interarrival.empty else float("nan"),
        "adf_pvalue": summary.adf_pvalue.iloc[0],
        "bds_pvalue": summary.bds_pvalue.iloc[0],
    }])
    metrics.to_csv(out / "metrics.csv", index=False)
    ax = summary[["received_packets", "loss_proxy"]].plot(subplots=True, layout=(2, 1), figsize=(9, 6), legend=False)
    ax[0, 0].set_ylabel("packets / window")
    ax[1, 0].set_ylabel("loss proxy")
    ax[1, 0].set_xlabel("window start (s)")
    plt.tight_layout()
    plt.savefig(out / "stability_timeseries.png", dpi=160)
    print(metrics.to_string(index=False))


if __name__ == "__main__":
    main()
