# ESP32-S3 smell classifier

A step-by-step tutorial that takes one analog gas sensor, an MQ-135, from raw voltage to a neural network running
on the ESP32-S3 itself. The finished demo tells clean air, breath, alcohol vapour and smoke apart from the shape
of the sensor's response over ten seconds, and announces the result on a buzzer.

![The ESP32-S3 board with the MQ-135 sensor module connected by a four-wire cable](docs/images/hardware.jpg)

*The hardware used to write the tutorial: a custom ESP32-S3-WROOM-1 N16R8 board with an on-board buzzer, and an
MQ-135 module on a four-wire cable. Any ESP32-S3 board wired as in [chapter 1](docs/01-hardware.md) works.*

```
MQ-135 → ADC (10 Hz) → 10 s window → ln(v / start) → 1-D CNN, int8 → vote over 5 s → 1, 2 or 3 beeps
```

Everything needed to repeat it is here: two firmwares (ESP-IDF), the recording tools, the training pipeline
(TensorFlow), one session of real recordings, and the trained model.

| Measured on the board | |
|---|---|
| Inference time | 2.3 ms, once a second |
| Model | 516 parameters, 6,944 bytes of flash |
| Working memory (tensor arena) | 3,836 bytes |
| Board output vs PC output | identical on 300 of 300 replayed windows |
| Events classified correctly | 29 of 32, after one recording session |

That last figure is optimistic: with a single session the model is tested on events from the same sitting it
was trained on. [Chapter 6](docs/06-training.md) explains what it gets wrong and why.

**This is a demonstration.** An MQ-135 cannot identify gases reliably and must not be used as a safety device.

## The tutorial

| Chapter | What you end up with |
|---|---|
| [1. Hardware](docs/01-hardware.md) | A wired, warmed-up sensor and an understanding of its signal |
| [2. Project setup](docs/02-project-setup.md) | ESP-IDF and a Python environment that build and flash |
| [3. Data logger](docs/03-data-logger.md) | A 10 Hz sensor stream, with the firmware explained |
| [4. Collecting a dataset](docs/04-collecting-data.md) | Labelled recordings of every class, across sessions |
| [5. Preprocessing](docs/05-preprocessing.md) | Fixed-size, normalised, labelled windows |
| [6. Training](docs/06-training.md) | A small network and an honest test of it |
| [7. Quantising and converting](docs/07-quantising.md) | A 7 kB integer model as a C array |
| [8. Inference on the board](docs/08-inference.md) | The model running on the ESP32-S3, checked against the PC |

Two interactive pages go with it. Open them in a browser after cloning; GitHub shows only their source.

- [docs/interactive/tutorial.html](docs/interactive/tutorial.html): the whole tutorial on one page, with a
  divider calculator, a window explorer over real recordings, a quantisation calculator and a simulator of the
  firmware's event logic.
- [docs/interactive/collection-guide.html](docs/interactive/collection-guide.html): a worksheet for one recording
  session, with a round planner, a range check and a warm-up timer.

## What you need

| | |
|---|---|
| Board | An ESP32-S3 board with its native USB port. Written on an ESP32-S3-WROOM-1 N16R8 |
| Sensor | MQ-135 module, powered from 5 V |
| Parts | 4.7 kΩ and 6.8 kΩ resistors for a divider; optionally a passive buzzer and a transistor |
| Software | ESP-IDF 6.0.1, Python 3.10 or newer, about 2 GB of disk for TensorFlow |
| Stimuli | Hand sanitizer or isopropyl alcohol with cotton swabs; incense or matches |

Wiring, and the one file to edit if yours differs, are in [chapter 1](docs/01-hardware.md).

## Quick start

The chapters explain every step. This is the short version, for Linux; on other systems only the serial port
name changes.

**1. Get the code and the Python environment** (a terminal without ESP-IDF activated):

```bash
git clone https://github.com/stimexpabs/esp32s3-smell-classifier.git && cd esp32s3-smell-classifier
python3 -m venv .venv && .venv/bin/pip install -r tools/requirements.txt -r training/requirements.txt
```

**2. Flash the data logger** (a second terminal, with ESP-IDF activated):

```bash
cd firmware/data_logger && idf.py -p /dev/ttyACM0 flash
```

**3. Watch the sensor,** after 5 minutes of warm-up. Stop with Ctrl+C:

```bash
.venv/bin/python tools/collect.py monitor
```

**4. Record a session,** two events per command, for each of `clean`, `breath`, `alcohol` and `smoke`. One beep
means apply the stimulus, two beeps mean take it away:

```bash
.venv/bin/python tools/collect.py record --label alcohol --session d1-desk --events 2
.venv/bin/python tools/check_dataset.py
```

**5. Train and convert:**

```bash
.venv/bin/python training/preprocess.py
.venv/bin/python training/train.py
.venv/bin/python training/convert.py
```

**6. Build and flash the classifier.** The first build compiles about 840 files; limiting the parallel jobs keeps
a PC with 8 GB of RAM responsive:

```bash
cd firmware/inference && idf.py reconfigure && ninja -C build -j 3
idf.py -p /dev/ttyACM0 flash monitor
```

**7. Check the board against the PC** (close the monitor first with Ctrl+]):

```bash
.venv/bin/python tools/replay.py --count 300
```

## Trying it without hardware

Steps 5 to 7 of the tutorial need no board. The repository includes one real session, 8 events per class in
`data/raw/`, so `preprocess.py`, `train.py` and `convert.py` run as they are and reproduce the figures in
chapters 5 to 7.

## Using your own sensor

The included recordings and model come from one particular sensor, in one room, on one evening. MQ sensors differ
from unit to unit, so treat the included model as an example and do not expect it to classify well on your
board. To train on your own data only, delete the included session first:

```bash
rm data/raw/*/d1-desk_*.csv
```

Then record at least three sessions on different days ([chapter 4](docs/04-collecting-data.md)). With three or
more sessions `train.py` tests on whole held-out sessions, and its accuracy figure becomes one you can trust.

## Repository layout

```
docs/                      the eight chapters, and docs/interactive/ for the two HTML pages
firmware/components/board/     board_config.h: every pin and resistor value
firmware/components/buzzer/    non-blocking buzzer driver shared by both firmwares
firmware/data_logger/      chapter 3: streams sensor readings over USB
firmware/inference/        chapter 8: classifies on the board and beeps
tools/                     collect.py (record), check_dataset.py (verify), plot.py (inspect), replay.py (board vs PC)
training/                  preprocess.py (windows), train.py (model), convert.py (int8 + C array)
training/model/            the trained model: model.keras and model.tflite
data/raw/<label>/          one CSV per recorded event
```

## Tested with

| | Version |
|---|---|
| ESP-IDF | 6.0.1 |
| esp-tflite-micro, esp-nn | 1.4.1 (pinned in `firmware/inference/dependencies.lock`) |
| Python | 3.12 |
| TensorFlow | 2.21 |
| Host | Ubuntu Linux |

## Troubleshooting

| Symptom | Cause and fix |
|---|---|
| `idf.py: command not found` | ESP-IDF is not activated in that terminal. See [chapter 2](docs/02-project-setup.md) |
| A tool cannot open the serial port | Another program holds it. Close the ESP-IDF monitor with Ctrl+] |
| `No module named matplotlib` or `tensorflow` | The command ran with ESP-IDF's Python. Use `.venv/bin/python` |
| `collect.py` reports no data | The board is running the inference firmware. Flash `firmware/data_logger` |
| `replay.py` reports no answer | The board is running the data logger. Flash `firmware/inference` |
| No output at all on a board with two USB sockets | Use the socket labelled USB, not UART |
| Right in Python, wrong on the board | Run `replay.py`. If outputs differ, rerun `convert.py`, rebuild and flash |
| The PC freezes during the first inference build | Too many parallel compile jobs. Use `ninja -C build -j 3` |

## License

[MIT](LICENSE). The recordings in `data/raw/` and the model in `training/model/` are covered by the same license.
