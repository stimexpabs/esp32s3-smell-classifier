# 7. Quantising and converting

The trained model stores its weights as 32-bit floating-point numbers and is saved in a format the board cannot
read. This chapter shrinks it to 8-bit integers and turns it into a C array that is compiled into the firmware.

Source: [convert.py](../training/convert.py)

```bash
.venv/bin/python training/convert.py
```

The repository already contains the converted model from the first session, so the inference firmware builds
without running this.

## Why integers

An 8-bit integer model is a quarter of the size, and integer arithmetic is what microcontroller libraries
optimise hardest: Espressif's ESP-NN routines use the ESP32-S3's vector instructions for exactly these
operations. For a 516-parameter model the speed hardly matters, but the method is the same one a larger model
would need, which is why the tutorial uses it.

## How quantisation works

Every tensor in the model gets two numbers, a **scale** and a **zero point**, that map real values onto the 256
levels an `int8` can hold:

```
real_value = scale × (int8_value − zero_point)
```

To choose them, the converter has to know what range of values actually flows through each layer. It finds out
by running a **representative dataset** through the model, here 300 real windows from the recordings:

```python
converter.optimizations = [tf.lite.Optimize.DEFAULT]
converter.representative_dataset = representative
converter.target_spec.supported_ops = [tf.lite.OpsSet.TFLITE_BUILTINS_INT8]
converter.inference_input_type = tf.int8
converter.inference_output_type = tf.int8
```

The last three lines demand integers **everywhere, including the input and output**. Without them the converter
would keep float inputs and add conversion steps, and the firmware would need float kernels after all.

## What it means for the firmware

The model now expects its 100 input samples as `int8`. The firmware computes the same `ln(v / ref)` features as
chapter 5 in floating point, then converts each one:

```
q = round(x / scale) + zero_point       clamped to −128…127
```

With the first session's data the converter chose **scale 0.014355 and zero point −108** for the input, so the
int8 range covers features from −0.29 to 3.37. The firmware must not hard-code these: it reads them from the
model at start-up, because they change every time the model is retrained.

The output works the same way in reverse. Its scale is 1/256 and its zero point −128, so an output byte of 127
means a probability of about 1.0.

## The operators the model uses

```
CONV_2D, EXPAND_DIMS, FULLY_CONNECTED, MAX_POOL_2D, MEAN, RESHAPE, SOFTMAX
```

The 1-D convolutions became `CONV_2D` with a height of one, wrapped in `EXPAND_DIMS` and `RESHAPE`, and global
average pooling became `MEAN`. TensorFlow Lite Micro only links the operators it is told about, so chapter 8
registers exactly these seven.

## From file to firmware

A microcontroller has no file system to load `model.tflite` from, so the script writes the bytes out as a C
array in `firmware/inference/main/model_data.cc`, with the class names in `model_data.h`. The array is aligned to
16 bytes because the interpreter reads it in place, straight from flash.

## Check

```
model.tflite: 6944 bytes
float and int8 models agree on 96.4% of 1170 windows
accuracy on the training windows: float 87.6%, int8 86.2%
```

- **Size:** under 7 kB, against 16 MB of flash.
- **Agreement:** the integer model gives the same answer as the float model on 96% of windows. The ones that
  differ are close calls where rounding tips the balance.
- **Accuracy cost:** 1.4 percentage points. If this gap ever grows to several points, the representative dataset
  is the first thing to check.

The script runs the integer model with TensorFlow Lite's **reference kernels**, which round exactly as the
board's library does. The default PC kernels are faster but can differ by one step in an intermediate result,
enough to flip a borderline window.

These accuracy figures are measured on the windows the model was trained on. They compare the two versions of the
model with each other; the honest estimate of how well it works is the cross-validation in chapter 6.
