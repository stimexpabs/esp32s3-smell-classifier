# 3. Data logger

A model is only as good as its data, so the first firmware does nothing but measure. It streams the sensor to the
PC, and a script turns that stream into labelled files.

## The firmware

Source: [data_logger_main.c](../firmware/data_logger/main/data_logger_main.c)

### Reading the ADC

```c
adc_oneshot_chan_cfg_t channel_config = {
    .atten = ADC_ATTEN_DB_12,
    .bitwidth = ADC_BITWIDTH_DEFAULT,
};
adc_oneshot_config_channel(adc, ADC_CHANNEL_5, &channel_config);
```

- **Oneshot** mode takes a reading when asked. At 10 Hz there is no need for the continuous (DMA) driver.
- **12 dB attenuation** gives the widest input range, about 0-3.1 V, which covers the 2.96 V the divider can
  deliver.
- **Calibration** (`adc_cali_create_scheme_curve_fitting`) converts raw counts to millivolts using correction
  values Espressif burns into each chip. Without it, two boards report different voltages for the same input.

### One sample is 64 reads

```c
for (int i = 0; i < OVERSAMPLE; i++) {
    adc_oneshot_read(adc, MQ135_ADC_CHANNEL, &raw);
    adc_cali_raw_to_voltage(cali, raw, &mv);
    sum_mv += mv;
}
```

Averaging removes most of the ADC's noise and gives sub-millivolt resolution, which matters because an unmodified
MQ-135 idles at only tens of millivolts. The firmware prints hundredths of a millivolt using integers only.

### Steady timing

```c
vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(SAMPLE_PERIOD_MS));
```

`vTaskDelayUntil` wakes the loop every 100 ms measured from the previous wake-up, so the time spent reading and
printing does not accumulate as drift. The model will assume evenly spaced samples.

**Why 10 Hz?** The sensor responds over seconds. Ten samples a second captures the rise of a response with room
to spare, and keeps the model's input small: a 10-second window is 100 numbers.

### Output format

```
D,<t_ms>,<adc_mv>,<true_mv>,<rs_kohm>    a sample: board time, pin voltage, sensor voltage, sensor resistance
M,<t_ms>,<0|1>                           a marker: stimulus off / on
```

The prefix lets the PC script pick data out from ordinary log lines.

`rs_kohm` is the sensor resistance from chapter 1, `Rs = RL_eff × (5 V − AO) / AO`. It depends on two constants
in the firmware: `MQ135_RL_OHM`, which must match the load resistor on your module, and a supply of exactly 5 V,
which USB power only approximates. The model does not use this column; it is there for comparing against the
datasheet and other MQ-135 projects.

### Markers and the buzzer

The PC sends `1` to start a stimulus and `0` to end it. The board beeps (once for on, twice for off) and prints a
marker stamped with **its own clock**. Because samples and markers share that clock, USB delays cannot shift a
label relative to the data.

The buzzer driver ([buzzer.c](../firmware/components/buzzer/buzzer.c)) times its beeps with an `esp_timer`
instead of a delay, so a beep never holds up the sampling loop.

## The PC tools

### Watch the signal

```bash
.venv/bin/python tools/collect.py monitor
```

Prints the pin voltage, the sensor voltage, the sensor resistance and the highest pin voltage seen so far.

Opening the serial port restarts the board, so the timestamps begin again from zero each time a tool starts, and
the tools wait a moment for it to boot. The
sensor's heater stays powered through the restart, so no new warm-up is needed.

### Record labelled events

```bash
.venv/bin/python tools/collect.py record --label alcohol --session day1 --events 5
```

Each event runs by itself:

| Phase | Default | What you do |
|---|---|---|
| baseline | 10 s | nothing; keep the sensor in clean air |
| expose | 5 s | **one beep:** hold the stimulus near the sensor |
| recover | 30 s or more | **two beeps:** take it away |

Recovery lasts until the reading is back within 10% of the baseline, up to 3 minutes, so the next event starts
from clean air. Each event is saved as `data/raw/<label>/<session>_<nn>.csv`:

```
t_ms,adc_mv,true_mv,rs_kohm,stimulus
1908,97.54,164.95,49.91,0
```

The label `clean` records the same length of time with no beeps.

### Look at what you recorded

```bash
.venv/bin/python tools/plot.py
```

One panel per class, every event overlaid, time aligned to the first beep and each trace divided by its own
baseline. This is the same normalisation the model will use.

## Check

1. In `monitor`, note the clean-air pin voltage after warm-up.
2. Hold a swab of hand sanitizer near the sensor for a few seconds: the reading should climb within a couple of
   seconds and take much longer to fall back.
3. Note the **peak pin voltage** for your strongest stimulus. It must stay below about 2900 mV; a signal that
   flattens at the top is clipped, and clipped responses all look alike to a model. If it clips, hold the stimulus
   further away.
4. Record two or three `alcohol` events and one `clean`, then run `plot.py`. The alcohol traces should rise after
   time zero and the clean trace should stay near 1.0.

Write the clean-air and peak values down. Chapter 4 uses them to decide how strong each stimulus should be.
