#!/usr/bin/env python3
"""Check that the board gives the same answers as the PC (tutorial stage 8).

    .venv/bin/python tools/replay.py            # 80 recorded windows
    .venv/bin/python tools/replay.py --count 200

Sends recorded 10 s windows of raw sensor voltage to the inference firmware, which normalises and
classifies them with its own code, and compares the int8 outputs with the same model run here.
A mismatch means the firmware's preprocessing or its model differs from the training pipeline.
"""
import argparse
import os
import sys
import time
from pathlib import Path

os.environ.setdefault("TF_CPP_MIN_LOG_LEVEL", "2")

import numpy as np
import serial
import tensorflow as tf

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "training"))
from preprocess import CLASSES, RAW_DIR, WINDOW, features, load  # noqa: E402

TFLITE_FILE = ROOT / "training" / "model" / "model.tflite"


def recorded_windows(count, rng):
    """Raw-voltage windows spread evenly over the classes, cut around each stimulus."""
    windows = []
    for label in CLASSES:
        for path in sorted((RAW_DIR / label).glob("*.csv")):
            v, onset = load(path)
            starts = range(0, len(v) - WINDOW + 1, 10)
            if onset is not None:
                starts = [s for s in starts if 10 <= onset - s <= WINDOW - 30] or list(starts)
            for s in starts:
                windows.append((f"{label}/{path.name}@{s / 10:.0f}s", v[s:s + WINDOW]))
    picks = rng.choice(len(windows), size=min(count, len(windows)), replace=False)
    return [windows[i] for i in sorted(picks)]


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--port", default="/dev/ttyACM0")
    parser.add_argument("--count", type=int, default=80)
    args = parser.parse_args()

    # The reference kernels round the way TensorFlow Lite Micro does; the default PC kernels can differ by one step.
    interpreter = tf.lite.Interpreter(model_path=str(TFLITE_FILE),
                                      experimental_op_resolver_type=tf.lite.experimental.OpResolverType.BUILTIN_REF)
    interpreter.allocate_tensors()
    inp, out = interpreter.get_input_details()[0], interpreter.get_output_details()[0]
    scale, zero = inp["quantization"]

    ser = serial.Serial()
    ser.port, ser.baudrate, ser.timeout = args.port, 115200, 3
    ser.dtr = ser.rts = False
    ser.open()
    time.sleep(1.5)   # opening the port restarts the board
    ser.reset_input_buffer()

    same_class = exact = 0
    worst = 0
    windows = recorded_windows(args.count, np.random.default_rng(0))
    for name, v in windows:
        q = np.clip(np.round(features(v) / scale) + zero, -128, 127).astype(np.int8)
        interpreter.set_tensor(inp["index"], q.reshape(inp["shape"]))
        interpreter.invoke()
        expected = interpreter.get_tensor(out["index"])[0].astype(int)

        ser.write(("R " + " ".join(f"{x:.2f}" for x in v) + "\n").encode())
        while True:
            line = ser.readline().decode("ascii", errors="ignore").strip()
            if not line:
                sys.exit(f"No answer from {args.port}. Is the inference firmware running?")
            if line.startswith("R,"):
                break
        if line == "R,error":
            sys.exit(f"The board rejected window {name}")
        got = np.array([int(x) for x in line.split(",")[1:]])

        diff = int(np.abs(got - expected).max())
        worst = max(worst, diff)
        exact += diff == 0
        same_class += got.argmax() == expected.argmax()
        if got.argmax() != expected.argmax():
            print(f"  {name}: board {CLASSES[got.argmax()]} {got.tolist()}, PC {CLASSES[expected.argmax()]} {expected.tolist()}")

    n = len(windows)
    print(f"{n} windows: same class on {same_class}/{n}, identical outputs on {exact}/{n}, "
          f"largest output difference {worst} of 255")


if __name__ == "__main__":
    main()
