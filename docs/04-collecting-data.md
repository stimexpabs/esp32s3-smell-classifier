# 4. Collecting a dataset

This chapter is done by hand, at the bench. The goal is a set of recordings in which the only thing that
separates the classes is the gas, and in which everything else varies as much as it will in real use.

## The classes

| Label | Stimulus | How to apply it |
|---|---|---|
| `clean` | none | leave the sensor alone, or disturb the air without adding a gas |
| `alcohol` | hand sanitizer or isopropyl alcohol | a damp cotton swab held 2-5 cm from the sensor mesh |
| `smoke` | an incense stick, or a match just blown out | let the smoke drift across the sensor from 5-10 cm |
| `breath` | exhaled air | breathe out slowly onto the sensor from about 5 cm |

Safety: alcohol vapour is flammable. Close the bottle and move it away before lighting anything, and record the
smoke events in a ventilated room.

## First, check the signal range

Before recording anything, find out how strongly your sensor reacts:

```bash
.venv/bin/python tools/collect.py monitor
```

Apply your strongest stimulus (usually alcohol, held close) and read the **peak pin** value.

- **Below about 2900 mV:** carry on.
- **At or near 2900 mV:** the signal is clipping. Hold the stimulus further away until the peak stays lower.

Then wait until the reading is back at its clean-air level.

## What a session is

A **session** is one sitting: one place, one day, one warm-up. Name it for what distinguishes it, such as
`d1-desk` or `d2-kitchen`. The name matters because chapter 5 splits the data **by session**: whole sessions go
to training, and other whole sessions are held back for testing. A model tested on a session it has never seen
tells you how it will behave tomorrow; a model tested on events from the same sitting only tells you it memorised
that sitting.

So you need **at least three sessions**, and five is better. Spread them over different days or rooms.

## How much to record

| | Per session | Minimum (3 sessions) | Better (5 sessions) |
|---|---|---|---|
| each stimulus class | 8 events | 24 | 40 |
| `clean` | 8 recordings | 24 | 40 |

One event takes 45 seconds plus recovery, so a full session is 30 to 45 minutes. The minimum is enough to train a
first model and see where it struggles; add sessions afterwards rather than trying to record everything at once.

## Running a session

1. Power the board and wait at least 5 minutes.
2. Record in rounds, two events of each class per round, so that no class sits only at the start or the end of
   the sitting, where the sensor's drift is different:

```bash
.venv/bin/python tools/collect.py record --label clean --session d1-desk --events 2
```

```bash
.venv/bin/python tools/collect.py record --label breath --session d1-desk --events 2
```

```bash
.venv/bin/python tools/collect.py record --label alcohol --session d1-desk --events 2
```

```bash
.venv/bin/python tools/collect.py record --label smoke --session d1-desk --events 2
```

3. Repeat the round four times. File numbers continue from where the last run stopped, so nothing is overwritten.
4. Air the room after each smoke event; smoke lingers and will leak into whatever you record next.

During each event: **one beep** means apply the stimulus, **two beeps** mean take it away. The script then waits
for the reading to come back within 10% of the baseline before it starts the next event.

## Put variety in on purpose

The model learns whatever differs between your classes. If every alcohol event is at 2 cm for exactly 5 seconds,
it learns "2 cm for 5 seconds", not "alcohol". Vary:

- **Distance and strength:** near and far, a fresh swab and a nearly dry one.
- **Duration:** add `--expose 3` or `--expose 8` to some runs.
- **Clean air that is not still:** for some `clean` recordings, wave a hand over the sensor, move the board, or
  blow a fan across it during the "expose" seconds. These teach the model that moving air is not a gas.

## Check the session

```bash
.venv/bin/python tools/check_dataset.py
```

It prints the number of events per class and session, the typical peak for each class, and a list of files that
look wrong:

| Flag | Meaning | What to do |
|---|---|---|
| `no-response` | the stimulus barely moved the reading | delete the file and record again, closer |
| `contaminated` | a `clean` recording moved by more than 15% | delete it, air the room, record again |
| `clipped` | the pin reached the top of the ADC range | delete it and record from further away |
| `no-recovery` | the recording ended well above its baseline | keep it, but wait longer before the next event |

To remove a flagged recording, delete its file.

Then look at the shapes:

```bash
.venv/bin/python tools/plot.py
```

## Check

- Every class has at least 24 events across at least 3 sessions.
- `check_dataset.py` lists no `no-response`, `contaminated` or `clipped` files.
- In `plot.py`, the classes look different to the eye: how fast they rise, how high they go, how slowly they
  fall. If two classes look the same to you, the model will struggle with them too, and that pair is the one to
  rethink before training.
