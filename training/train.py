#!/usr/bin/env python3
"""Train and evaluate the smell classifier (tutorial stage 6).

    train.py            # cross-validate, print a confusion matrix, save training/model/model.keras

How the model is tested depends on how much data exists:
  - 3 or more sessions: each session in turn is held out and the model is trained on the others.
    This is the honest figure, because the test session was recorded on a different day.
  - fewer: whole events are held out instead. Every window of one event stays on the same side
    of the split, but train and test share a session, so the figure is optimistic.
"""
import os
from pathlib import Path

os.environ.setdefault("TF_CPP_MIN_LOG_LEVEL", "2")

import numpy as np
import tensorflow as tf
from sklearn.model_selection import LeaveOneGroupOut, StratifiedGroupKFold

ROOT = Path(__file__).resolve().parent.parent
DATA_FILE = ROOT / "data" / "processed" / "windows.npz"
MODEL_FILE = ROOT / "training" / "model" / "model.keras"
EPOCHS = 80
SEED = 7


def build_model(n_classes):
    return tf.keras.Sequential([
        tf.keras.layers.Input(shape=(100, 1)),
        tf.keras.layers.Conv1D(8, 5, activation="relu"),
        tf.keras.layers.MaxPooling1D(2),
        tf.keras.layers.Conv1D(16, 3, activation="relu"),
        tf.keras.layers.GlobalAveragePooling1D(),
        tf.keras.layers.Dense(n_classes, activation="softmax"),
    ])


def fit(X, y, n_classes):
    tf.keras.utils.set_random_seed(SEED)
    model = build_model(n_classes)
    model.compile(optimizer=tf.keras.optimizers.Adam(2e-3), loss="sparse_categorical_crossentropy")
    # Weight each class inversely to its size so the plentiful clean windows do not dominate.
    counts = np.bincount(y, minlength=n_classes)
    weights = {i: len(y) / (n_classes * c) for i, c in enumerate(counts) if c}
    model.fit(X, y, epochs=EPOCHS, batch_size=32, class_weight=weights, verbose=0)
    return model


def report(title, classes, truth, predicted):
    n = len(classes)
    matrix = np.zeros((n, n), dtype=int)
    for t, p in zip(truth, predicted):
        matrix[t, p] += 1
    print(f"\n{title}  (rows: true class, columns: predicted)")
    print(f"{'':10s}" + "".join(f"{c:>9s}" for c in classes) + f"{'recall':>9s}")
    for i, c in enumerate(classes):
        recall = matrix[i, i] / max(matrix[i].sum(), 1)
        print(f"{c:10s}" + "".join(f"{v:9d}" for v in matrix[i]) + f"{recall:9.0%}")
    print(f"accuracy {np.trace(matrix) / matrix.sum():.0%}, "
          f"balanced accuracy {np.mean([matrix[i, i] / max(matrix[i].sum(), 1) for i in range(n)]):.0%}")


def main():
    data = np.load(DATA_FILE)
    X, y, event, session = data["X"][..., None], data["y"], data["event"], data["session"]
    classes = [str(c) for c in data["classes"]]
    event_names = [str(n) for n in data["event_names"]]
    sessions = sorted(set(session))

    if len(sessions) >= 3:
        print(f"Testing on each of {len(sessions)} sessions in turn.")
        splits = LeaveOneGroupOut().split(X, y, session)
    else:
        print(f"Only {len(sessions)} session(s): holding out whole events instead. Expect this to flatter the model.")
        splits = StratifiedGroupKFold(n_splits=4, shuffle=True, random_state=SEED).split(X, y, event)

    predicted = np.zeros_like(y)
    for fold, (train, test) in enumerate(splits, 1):
        model = fit(X[train], y[train], len(classes))
        predicted[test] = model.predict(X[test], verbose=0).argmax(1)
        print(f"  fold {fold}: {np.mean(predicted[test] == y[test]):.0%} of {len(test)} windows")

    report("Windows", classes, y, predicted)

    # One verdict per recorded event: the most common prediction over its stimulus windows.
    truth_e, pred_e, missed = [], [], []
    for e, name in enumerate(event_names):
        label = classes.index(name.split("/")[0])
        mask = (event == e) & (y == label) if label else (event == e)
        if not mask.any():
            continue
        vote = np.bincount(predicted[mask], minlength=len(classes)).argmax()
        truth_e.append(label)
        pred_e.append(vote)
        if vote != label:
            missed.append(f"{name} -> {classes[vote]}")
    report("Events", classes, truth_e, pred_e)
    if missed:
        print("misclassified events: " + ", ".join(missed))

    model = fit(X, y, len(classes))
    MODEL_FILE.parent.mkdir(parents=True, exist_ok=True)
    model.save(MODEL_FILE)
    print(f"\nFinal model trained on all {len(X)} windows -> {MODEL_FILE.relative_to(ROOT)} ({model.count_params()} parameters)")


if __name__ == "__main__":
    main()
