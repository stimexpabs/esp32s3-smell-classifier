# 5. Preprocessing

The recordings are long voltage traces of different lengths. A neural network needs fixed-size inputs with a
label each. This chapter turns one into the other, and fixes the exact arithmetic that the firmware will have to
repeat in chapter 8.

Source: [preprocess.py](../training/preprocess.py)

```bash
.venv/bin/python training/preprocess.py
```

## Windows

The model looks at **10 seconds of signal at a time: 100 samples**. That is long enough to hold the rise of a
response and the start of its fall, and short enough that the board can answer within a few seconds of an event.

A recording is cut into many overlapping windows by sliding the 10-second frame along it. On the board the same
thing happens live: a new window ends at every new sample.

## Normalising: relative, and on a log scale

Raw voltage is useless as an input. During the first session the clean-air level fell from about 180 mV to
40 mV in 45 minutes, a change larger than most of the responses being classified. So each window is expressed
relative to **its own first second**:

```
ref  = mean(v[0..9])                    the window's reference level
x[i] = clip( ln(v[i] / ref), -1, 4 )    for all 100 samples
```

- **Dividing by `ref`** removes the baseline, whatever it happens to be that day. It also cancels the board's
  divider and the module's load resistor, which is why the model does not need Rs.
- **The logarithm** compresses the range. Alcohol reached 29 times its baseline and breath about 1.2 times; on a
  log scale those are 3.4 and 0.2, both comfortably visible. It also makes a doubling look the same whether it
  starts from 40 mV or 180 mV.
- **Clipping to −1…4** bounds the input. Chapter 7 squeezes these numbers into 8 bits, and a fixed range keeps
  that step predictable.

The rule this follows is the important one: **a window is normalised using only what is inside the window.** The
recording tools know when the stimulus started; the board never will. Anything computed here from outside the
window could not be reproduced on the device.

## Labelling

Each recording has a stimulus marker, so the position of the onset inside every window is known:

| Where the onset falls | Label |
|---|---|
| after the window ends (the window is all baseline) | `clean` |
| 1 to 7 s into the window, leaving at least 3 s of response | the recording's class |
| more than 15 s before the window starts (only the recovery is visible) | `clean` |
| anywhere else | not used |

Two of these deserve an explanation.

**Recovery is labelled `clean`.** A window that shows only a signal falling back contains no new event. Labelling
it `clean` teaches the model "nothing new is happening", so the board announces an event once, when it starts,
and not for the whole minute the sensor takes to recover.

**The windows in between are dropped.** With the onset in the last three seconds of a window there is too little
response to tell the classes apart, and just after the event the signal is still changing fast. No label would
be honest for these, so they are left out of training.

## Balance

Clean windows are far more plentiful than event windows, because every recording is mostly recovery. The script
takes a clean window every 2 seconds and an event window every 0.2 seconds, which brings the classes within a
factor of two of each other. Training corrects the rest with class weights.

## Output

`data/processed/windows.npz` holds:

| Array | Meaning |
|---|---|
| `X` | windows × 100 normalised samples |
| `y` | class index: 0 `clean`, 1 `breath`, 2 `alcohol`, 3 `smoke` |
| `event` | which recording each window came from |
| `session` | which session each window came from |

`event` and `session` are kept because chapter 6 must split the data by them. Overlapping windows from one event
are near-copies of each other; if some landed in training and others in testing, the test would only measure
memory.

## Check

After the first session the script reports:

```
1170 windows from 32 recordings -> data/processed/windows.npz
  clean      450 windows
  breath     240 windows
  alcohol    240 windows
  smoke      240 windows
```
