#!/usr/bin/env python3
"""Watch or record the MQ-135 stream from the data_logger firmware.

    collect.py monitor
    collect.py record --label alcohol --session day1 --events 10

`record` writes one CSV per event to data/raw/<label>/<session>_<nn>.csv with the columns
t_ms, adc_mv, true_mv, rs_kohm, stimulus. Each event is: a quiet baseline, one beep (apply the stimulus),
two beeps (take it away), then recovery until the reading is back near the baseline.
The label `clean` records the same length of time with no beeps and no stimulus.
"""
import argparse
import csv
import statistics
import sys
import time
from pathlib import Path

import serial

DATA_DIR = Path(__file__).resolve().parent.parent / "data" / "raw"


def open_port(port):
    ser = serial.Serial()
    ser.port = port
    ser.baudrate = 115200
    ser.timeout = 2
    ser.dtr = False
    ser.rts = False
    ser.open()
    # Opening the port restarts the board anyway. Wait for it to boot, then drop the lines from before the restart,
    # whose timestamps belong to the previous run.
    time.sleep(1.5)
    ser.reset_input_buffer()
    return ser


def stream(ser):
    """Yield ('D', t_ms, adc_mv, true_mv, rs_kohm) samples and ('M', t_ms, flag) markers; skip log lines."""
    while True:
        line = ser.readline().decode("ascii", errors="ignore").strip()
        if not line:
            sys.exit(f"\nNo data from {ser.port} for {ser.timeout:.0f} s. Is the data_logger firmware running?")
        parts = line.split(",")
        try:
            if parts[0] == "D" and len(parts) == 5:
                yield "D", int(parts[1]), float(parts[2]), float(parts[3]), float(parts[4])
            elif parts[0] == "M" and len(parts) == 3:
                yield "M", int(parts[1]), int(parts[2])
        except ValueError:
            pass  # a line cut in half by a reset or a log message


def monitor(ser):
    peak = 0.0
    for msg in stream(ser):
        if msg[0] != "D":
            continue
        _, t_ms, adc_mv, true_mv, rs_kohm = msg
        peak = max(peak, adc_mv)
        bar = "#" * int(adc_mv / 3100 * 60)
        print(f"{t_ms / 1000:8.1f} s  pin {adc_mv:7.2f} mV  sensor {true_mv:7.2f} mV  Rs {rs_kohm:6.2f} k  peak pin {peak:7.2f} mV  {bar}")


def record_event(ser, args):
    """Run one event and return its rows."""
    clean = args.label == "clean"
    rows = []
    stimulus = 0
    phase = "baseline"
    t_start = t_phase = baseline = None

    ser.reset_input_buffer()
    for msg in stream(ser):
        if msg[0] == "M":
            stimulus = msg[2]
            continue
        _, t_ms, adc_mv, true_mv, rs_kohm = msg
        if t_start is None:
            t_start = t_phase = t_ms
        rows.append((t_ms, adc_mv, true_mv, rs_kohm, stimulus))
        in_phase = (t_ms - t_phase) / 1000

        if phase == "baseline" and in_phase >= args.pre:
            baseline = statistics.median(r[2] for r in rows)
            if not clean:
                ser.write(b"1")
            phase, t_phase = "expose", t_ms
        elif phase == "expose" and in_phase >= args.expose:
            if not clean:
                ser.write(b"0")
            phase, t_phase = "recover", t_ms
        elif phase == "recover" and in_phase >= args.post:
            recovered = true_mv <= baseline * 1.10
            if clean or recovered or in_phase >= args.max_recover:
                print()
                if not (clean or recovered):
                    print(f"  not back to baseline after {args.max_recover} s; wait longer before the next event")
                return rows

        hint = {"baseline": "keep clear", "expose": "APPLY STIMULUS", "recover": "remove, let it recover"}[phase]
        if clean:
            hint = "keep clear"
        print(f"\r  {(t_ms - t_start) / 1000:6.1f} s  {true_mv:8.2f} mV  {phase:8s}  {hint:24s}", end="", flush=True)


def record(ser, args):
    out_dir = DATA_DIR / args.label
    out_dir.mkdir(parents=True, exist_ok=True)
    # Continue the numbering so an existing recording is never overwritten.
    taken = [int(p.stem.rsplit("_", 1)[1]) for p in out_dir.glob(f"{args.session}_*.csv")]
    first = max(taken, default=0) + 1

    for n in range(first, first + args.events):
        path = out_dir / f"{args.session}_{n:02d}.csv"
        print(f"Event {n - first + 1}/{args.events} -> {path.relative_to(DATA_DIR.parent.parent)}")
        rows = record_event(ser, args)
        with path.open("w", newline="") as f:
            writer = csv.writer(f)
            writer.writerow(["t_ms", "adc_mv", "true_mv", "rs_kohm", "stimulus"])
            writer.writerows(rows)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--port", default="/dev/ttyACM0")
    sub = parser.add_subparsers(dest="command", required=True)
    sub.add_parser("monitor", help="print live readings")
    rec = sub.add_parser("record", help="record labelled events")
    rec.add_argument("--label", required=True, help="class name, e.g. clean, alcohol, smoke, breath")
    rec.add_argument("--session", required=True, help="name for this sitting, e.g. day1-desk")
    rec.add_argument("--events", type=int, default=5)
    rec.add_argument("--pre", type=float, default=10, help="baseline seconds before the stimulus")
    rec.add_argument("--expose", type=float, default=5, help="seconds the stimulus is applied")
    rec.add_argument("--post", type=float, default=30, help="minimum recovery seconds")
    rec.add_argument("--max-recover", type=float, default=180, help="give up waiting for recovery after this long")
    args = parser.parse_args()

    ser = open_port(args.port)
    try:
        if args.command == "monitor":
            monitor(ser)
        else:
            record(ser, args)
    except KeyboardInterrupt:
        print()
    finally:
        ser.close()


if __name__ == "__main__":
    main()
