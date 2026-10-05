# 8. Inference on the board

The last firmware does everything on the ESP32-S3: it samples the sensor, normalises a window, runs the model
and announces the result. The PC is only there to show the output.

Source: [inference_main.cc](../firmware/inference/main/inference_main.cc)

## TensorFlow Lite Micro

TensorFlow Lite Micro (TFLM) is an interpreter for `.tflite` models written for microcontrollers: no operating
system, no file system, no dynamic memory. Espressif packages it as the `esp-tflite-micro` component, together
with ESP-NN, their optimised kernels for the ESP32-S3. One line in
[idf_component.yml](../firmware/inference/main/idf_component.yml) pulls both in:

```yaml
dependencies:
  espressif/esp-tflite-micro: "*"
```

TFLM is C++, so this firmware's main file is `inference_main.cc` with `extern "C" void app_main(void)`.

## Build and flash

The first build compiles about 840 files, most of them TFLM. Compiling them all at once can exhaust the memory
of a PC with 8 GB of RAM, so configure first and then build with a limited number of parallel jobs:

```bash
cd firmware/inference && idf.py reconfigure
```

```bash
ninja -C build -j 3
```

```bash
idf.py -p /dev/ttyACM0 flash monitor
```

Later builds only recompile what changed and can use plain `idf.py build`.

## Setting up the interpreter

```cpp
static tflite::MicroMutableOpResolver<7> resolver;
resolver.AddConv2D();
resolver.AddExpandDims();
resolver.AddFullyConnected();
resolver.AddMaxPool2D();
resolver.AddMean();
resolver.AddReshape();
resolver.AddSoftmax();

static tflite::MicroInterpreter interpreter(model, resolver, s_arena, TENSOR_ARENA_BYTES);
interpreter.AllocateTensors();
```

- **The resolver** lists the seven operators from chapter 7. Only these are linked into the firmware. Leave one
  out and `AllocateTensors` fails at start-up.
- **The tensor arena** is a plain byte array that holds every intermediate result. TFLM never calls `malloc`. The
  firmware gives it 16 kB and logs how much it needed: **3,836 bytes**.
- **The model** is read in place from flash. Nothing is copied to RAM.

## From voltages to model input

Samples go into a ring buffer of 100. Once a second the buffer is unrolled, oldest sample first, and passed to
`classify()`, which repeats chapter 5 line for line:

```cpp
float x = logf(v[i] / ref);                              // ref = mean of the first 10 samples
x = fminf(fmaxf(x, MODEL_CLIP_LO), MODEL_CLIP_HI);       // clip to -1 ... 4
long q = lroundf(x / scale) + zero_point;                // to int8, clamped to -128 ... 127
```

The window length, reference length and clip limits come from `model_data.h`, which `convert.py` writes from the
constants in `preprocess.py`. The scale and zero point are read from the model itself. Nothing about the
preprocessing is typed in twice, so retraining cannot leave the firmware out of step.

## From predictions to events

The model answers once a second, and a single answer is not an event. While a response is still rising, the
window holds only its first second or two, and the model has not seen windows like that in training. So the
firmware waits and votes:

1. **Idle.** Two non-clean predictions in a row open a vote. A prediction below 60% confidence counts as clean.
2. **Voting.** The next five predictions are collected. If at least three are non-clean, the class with the most
   votes is announced; otherwise it was a blip and nothing happens.
3. **Hold-off.** For 20 seconds no new event is announced, while the sensor recovers.

An announcement prints `>>> EVENT: <class>` and beeps: **once for breath, twice for alcohol, three times for
smoke**. Expect it about 6 to 8 seconds after the stimulus starts.

## Output

One line per second:

```
   12.9 s     36.0 mV  clean     87%   | clean 87% breath 13% alcohol 1% smoke 0% | 2270 us
```

Time since boot, sensor voltage, the winning class and its probability (with a `?` when it is below 60%), all
four probabilities, and how long the interpreter took. The first line appears after 10 seconds, when the buffer
is full.

## Check 1: the board agrees with the PC

A model that works in Python and fails on the board is nearly always a preprocessing mismatch, and looking at
live output will not reveal it. The replay test will. It sends recorded windows of **raw voltage** to the board,
which runs them through its own normalisation and model, and compares the answers with the PC's:

```bash
.venv/bin/python tools/replay.py --count 300
```

```
300 windows: same class on 300/300, identical outputs on 300/300, largest output difference 0 of 255
```

Every output byte is identical. That proves the feature maths, the quantisation and the model on the board are
the ones that were trained and tested.

The comparison uses TensorFlow Lite's reference kernels on the PC. Against the default PC kernels, 2 of 80
windows came out as a different class: both were borderline cases where the two candidates were within a few
steps of each other, and one step of rounding inside the PC's faster kernels tipped them.

## Check 2: cost

| | Measured |
|---|---|
| Inference time | 2.3 ms, once a second |
| Tensor arena | 3,836 bytes |
| Model | 6,944 bytes of flash |
| Firmware image | 304 kB |

The model uses about 0.2% of the processor's time. The 8 MB of PSRAM is not needed for it at all.

## Check 3: live

With the monitor running, wait 5 minutes for the sensor to warm up, then apply each stimulus in turn and leave
half a minute between them:

- breath should give one beep, alcohol two, smoke three;
- clean air, and waving a hand over the sensor, should give none.

Write down what it gets wrong. Those cases say what the next recording session should contain.

## Going back to recording

The board runs one firmware at a time. To record more data, flash the logger again:

```bash
cd firmware/data_logger && idf.py -p /dev/ttyACM0 flash
```

After a new session, the full update is: `preprocess.py`, `train.py`, `convert.py`, then rebuild and flash
`firmware/inference`. Only `model_data.cc` changes, so that rebuild takes seconds.
