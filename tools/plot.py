#!/usr/bin/env python3
"""Overlay every recorded event, one panel per class.

    plot.py                 # show a window
    plot.py --save out.png  # write an image instead

Time is aligned to the moment the stimulus was applied, and each trace is divided by its own
pre-stimulus baseline, so the panels show the shape of the response rather than the absolute voltage.
"""
import argparse
import csv
import statistics
from pathlib import Path

import matplotlib.pyplot as plt

DATA_DIR = Path(__file__).resolve().parent.parent / "data" / "raw"


def load(path):
    with path.open() as f:
        rows = [(int(r["t_ms"]), float(r["true_mv"]), int(r["stimulus"])) for r in csv.DictReader(f)]
    onset = next((i for i, r in enumerate(rows) if r[2]), None)
    # Clean recordings have no stimulus: use their first 10 s as the baseline, like the stimulus classes.
    base_rows = rows[:onset] if onset else rows[:100]
    baseline = statistics.median(r[1] for r in base_rows)
    t0 = rows[onset][0] if onset else rows[len(base_rows) - 1][0]
    return [(r[0] - t0) / 1000 for r in rows], [r[1] / baseline for r in rows]


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--save", help="write the figure to this file instead of opening a window")
    args = parser.parse_args()

    labels = sorted(p.name for p in DATA_DIR.iterdir() if p.is_dir() and any(p.glob("*.csv")))
    if not labels:
        raise SystemExit(f"No recordings under {DATA_DIR}")

    fig, axes = plt.subplots(len(labels), 1, sharex=True, sharey=True, figsize=(9, 2.6 * len(labels)), squeeze=False)
    for ax, label in zip(axes[:, 0], labels):
        files = sorted((DATA_DIR / label).glob("*.csv"))
        for path in files:
            t, ratio = load(path)
            ax.plot(t, ratio, linewidth=0.8, alpha=0.7)
        ax.axvline(0, color="gray", linestyle=":", linewidth=0.8)
        ax.set_title(f"{label} ({len(files)} events)", loc="left", fontsize=10)
        ax.set_ylabel("V / baseline")
        ax.grid(alpha=0.3)
    axes[-1, 0].set_xlabel("seconds from stimulus")
    fig.tight_layout()

    if args.save:
        fig.savefig(args.save, dpi=130)
    else:
        plt.show()


if __name__ == "__main__":
    main()
