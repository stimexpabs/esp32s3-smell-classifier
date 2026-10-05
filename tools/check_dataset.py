#!/usr/bin/env python3
"""Summarise the recordings in data/raw and flag the ones worth re-recording.

    check_dataset.py            # totals per class and session, plus problem files
    check_dataset.py --all      # also list every file

Flags:
    no-response   a stimulus event whose peak is under 1.15x its baseline
    contaminated  a clean recording that moves more than 15% from its baseline
    clipped       the pin voltage reached the top of the ADC range
    no-recovery   the recording ended more than 10% above its baseline
"""
import argparse
import csv
import statistics
from collections import defaultdict
from pathlib import Path

DATA_DIR = Path(__file__).resolve().parent.parent / "data" / "raw"
CLIP_PIN_MV = 2900


def inspect(path, label):
    with path.open() as f:
        rows = [(float(r["adc_mv"]), float(r["true_mv"]), int(r["stimulus"])) for r in csv.DictReader(f)]
    onset = next((i for i, r in enumerate(rows) if r[2]), None)
    baseline = statistics.median(r[1] for r in (rows[:onset] if onset else rows[:100]))
    after = [r[1] for r in rows[onset or 0:]]
    peak = max(after) / baseline
    low = min(after) / baseline

    flags = []
    if label == "clean":
        if peak > 1.15 or low < 0.85:
            flags.append("contaminated")
    else:
        if onset is None or peak < 1.15:
            flags.append("no-response")
        if rows[-1][1] > baseline * 1.10:
            flags.append("no-recovery")
    if max(r[0] for r in rows) >= CLIP_PIN_MV:
        flags.append("clipped")
    return baseline, peak, len(rows) / 10, flags


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--all", action="store_true", help="list every file, not only the flagged ones")
    args = parser.parse_args()

    counts = defaultdict(lambda: defaultdict(int))   # label -> session -> events
    peaks = defaultdict(list)
    lines = []
    for label_dir in sorted(p for p in DATA_DIR.iterdir() if p.is_dir()):
        for path in sorted(label_dir.glob("*.csv")):
            label, session = label_dir.name, path.stem.rsplit("_", 1)[0]
            baseline, peak, seconds, flags = inspect(path, label)
            counts[label][session] += 1
            peaks[label].append(peak)
            if flags or args.all:
                lines.append(f"  {label + '/' + path.name:32s} baseline {baseline:7.1f} mV  peak x{peak:5.2f}  "
                             f"{seconds:5.0f} s  {' '.join(flags)}")
    if not counts:
        raise SystemExit(f"No recordings under {DATA_DIR}")

    sessions = sorted({s for per_label in counts.values() for s in per_label})
    print(f"{'class':10s}" + "".join(f"{s:>12s}" for s in sessions) + f"{'total':>8s}{'median peak':>14s}")
    for label, per_session in counts.items():
        print(f"{label:10s}" + "".join(f"{per_session.get(s, 0):12d}" for s in sessions)
              + f"{sum(per_session.values()):8d}{'x' + format(statistics.median(peaks[label]), '.2f'):>14s}")
    if lines:
        print("\nFiles:" if args.all else "\nFlagged files:")
        print("\n".join(lines))
    else:
        print("\nNo files flagged.")


if __name__ == "__main__":
    main()
