# 6. Training

Source: [train.py](../training/train.py)

## Environment

The scripts use the virtual environment created in chapter 2:

```bash
.venv/bin/python training/preprocess.py && .venv/bin/python training/train.py
```

## The model

```
input            100 samples × 1 channel
Conv1D           8 filters, 5 samples wide, ReLU      slopes and steps: "rising fast", "falling slowly"
MaxPooling1D     halves the length
Conv1D           16 filters, 3 wide, ReLU             combinations: "sharp rise then slow fall"
GlobalAveragePooling1D                                how much of each pattern the window holds, wherever it is
Dense            4 outputs, softmax                   one probability per class
```

It has **516 parameters**. That is tiny on purpose:

- A convolution slides the same small filter along the window, so a rise is recognised wherever it occurs. This
  matters because on the board the event can start anywhere in the window.
- Global average pooling replaces a large dense layer. A dense layer over 100 positions would hold most of the
  parameters and would learn where in the window the training events happened to be.
- With a few hundred windows of training data, a larger model would memorise them.

Every layer here has an integer implementation in TensorFlow Lite Micro, which chapter 8 relies on.

## Testing it honestly

Accuracy on the data a model was trained on says nothing. The question is how it does on data it has not seen,
and how "unseen" is defined decides how believable the answer is.

- **With three or more sessions**, the script holds out one whole session at a time, trains on the rest, and tests
  on the held-out one. That measures what matters: a model trained on other days, used today.
- **With fewer**, it holds out whole events instead. All windows from one event stay together, but training and
  test share a sitting, the same room air and the same drift. The script says so when it runs, and the figure it
  prints is optimistic.

Two tables are printed. **Windows** counts every 10-second window. **Events** takes one vote per recording, which
is closer to what the board does when it smooths its predictions over a few seconds.

## Result after the first session

One session of 8 events per class, tested by holding out events:

```
Windows       clean   breath  alcohol    smoke   recall
clean           418       29        0        3      93%
breath           54      186        0        0      78%
alcohol           7        0      200       33      83%
smoke             2        0       32      206      86%
accuracy 86%, balanced accuracy 85%

Events: 29 of 32 correct (91%)
misclassified: breath/d1-desk_03 -> clean, alcohol/d1-desk_02 -> smoke, smoke/d1-desk_07 -> alcohol
```

Reading a confusion matrix: each row is what the window really was, each column what the model said. The
diagonal is correct; everything off it is a specific mistake, and the mistakes are more useful than the score.

- **Breath and clean are confused with each other.** Breath raises the signal by about 1.2 times, and clean air
  drifted by nearly as much during the session. The missed breath event is one where the sensor did not respond.
- **Alcohol and smoke are confused with each other, and the three wrong events show why.** The weakest alcohol
  event (3 times baseline) was called smoke, and the strongest smoke event (7 times) was called alcohol. The
  model is leaning on **how big** the response is, and size depends on how close the stimulus was, not on what it
  was.
- **Nothing strong is mistaken for clean.** Detecting that an event happened is the easy part.

## What the next sessions should add

The second point is the one to act on. A model can only learn that size is not the answer if the data shows it:

- record some **alcohol from further away**, with peaks of only 2 to 5 times the baseline;
- record some **smoke from close up**, with peaks of 5 to 10 times;
- hold the **breath** stimulus closer, so every event moves the reading by at least 1.15 times.

If alcohol and smoke still cannot be told apart once their sizes overlap, the sensor's response shape does not
carry the difference, and the honest fix is to merge or drop a class.

## Check

`train.py` finishes by training on all the data and saving `training/model/model.keras`. Rerun both scripts
after every new session and watch the confusion matrix, not the single accuracy figure.
