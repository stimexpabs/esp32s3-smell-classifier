#!/usr/bin/env python3
"""Turn the recordings in data/raw into labelled windows for training (tutorial stage 5).

    preprocess.py            # writes data/processed/windows.npz and prints a summary

One window is 10 s of sensor voltage (100 samples). It becomes the model input by

    x[i] = clip(ln(v[i] / mean(v[0:10])), -1, 4)

that is, every sample relative to the window's own first second, on a log scale. The firmware
repeats exactly this in C, so nothing here may use information from outside the window.
"""
import csv
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parent.parent
RAW_DIR = ROOT / "data" / "raw"
OUT_FILE = ROOT / "data" / "processed" / "windows.npz"

CLASSES = ["clean", "breath", "alcohol", "smoke"]
SAMPLE_HZ = 10
WINDOW = 100          # samples: 10 s
REF = 10              # samples averaged into the window's reference level: 1 s
EVENT_STRIDE = 2      # samples between windows around a stimulus: 0.2 s
CLEAN_STRIDE = 20     # samples between clean windows, which are plentiful: 2 s
CLIP_LO, CLIP_HI = -1.0, 4.0

# Where the stimulus onset must fall inside a window for the window to carry the event's label:
# at least 1 s of clean air before it, and at least 3 s of response after it.
ONSET_MIN = 10
ONSET_MAX = WINDOW - 30
# Windows starting this long after the onset show only the recovery. They are labelled clean,
# meaning "no new event", so the model does not announce an event twice.
TAIL_START = 150


def features(v):
    """Model input for one window of voltages. Mirrored by the firmware."""
    ref = np.mean(v[:REF])
    return np.clip(np.log(v / ref), CLIP_LO, CLIP_HI).astype(np.float32)


def load(path):
    with path.open() as f:
        rows = [(float(r["true_mv"]), int(r["stimulus"])) for r in csv.DictReader(f)]
    v = np.array([r[0] for r in rows])
    stim = np.array([r[1] for r in rows])
    onset = int(np.argmax(stim)) if stim.any() else None
    return v, onset


def windows_of(v, onset, label):
    """Yield (features, class index) for every usable window of one recording."""
    for start in range(0, len(v) - WINDOW + 1, EVENT_STRIDE):
        if onset is None or start + WINDOW <= onset or start >= onset + TAIL_START:
            if start % CLEAN_STRIDE:
                continue
            y = 0
        elif ONSET_MIN <= onset - start <= ONSET_MAX:
            y = CLASSES.index(label)
        else:
            continue   # onset too close to an edge, or still inside the response: ambiguous
        yield features(v[start:start + WINDOW]), y


def main():
    X, y, event, session = [], [], [], []
    names = []
    for label in CLASSES:
        for path in sorted((RAW_DIR / label).glob("*.csv")):
            v, onset = load(path)
            if label != "clean" and onset is None:
                print(f"skipping {label}/{path.name}: no stimulus marker")
                continue
            for feats, cls in windows_of(v, onset, label):
                X.append(feats)
                y.append(cls)
                event.append(len(names))
                session.append(path.stem.rsplit("_", 1)[0])
            names.append(f"{label}/{path.name}")

    X, y, event = np.stack(X), np.array(y), np.array(event)
    OUT_FILE.parent.mkdir(parents=True, exist_ok=True)
    np.savez_compressed(OUT_FILE, X=X, y=y, event=event, session=np.array(session),
                        event_names=np.array(names), classes=np.array(CLASSES))

    print(f"{len(X)} windows from {len(names)} recordings -> {OUT_FILE.relative_to(ROOT)}")
    for i, name in enumerate(CLASSES):
        print(f"  {name:8s} {int((y == i).sum()):5d} windows")
    print(f"  sessions: {', '.join(sorted(set(session)))}")


if __name__ == "__main__":
    main()
