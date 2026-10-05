# 2. Project setup

## Get the code

```bash
git clone https://github.com/stimexpabs/esp32s3-smell-classifier.git && cd esp32s3-smell-classifier
```

## Toolchain

The firmware is built with **ESP-IDF 6.0.1**. Install it by following Espressif's
[Get Started guide](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/get-started/index.html), then
activate it in every new terminal before using `idf.py`. The command depends on how ESP-IDF was installed:

```bash
. ~/.espressif/tools/activate_idf_v6.0.1.sh
```

That is the script the Espressif Installation Manager creates. With a manual installation it is
`. $HOME/esp/esp-idf/export.sh` instead. Either way, `idf.py --version` should then print the version.

## Python environment for the PC side

The recording tools and the training scripts need Python 3.10 or newer. Create a virtual environment inside the
project, in a terminal where ESP-IDF is **not** activated:

```bash
python3 -m venv .venv && .venv/bin/pip install -r tools/requirements.txt -r training/requirements.txt
```

This downloads TensorFlow and takes about 2 GB of disk. All commands in the tutorial that start with
`.venv/bin/python` use it. Activating ESP-IDF switches `python3` to ESP-IDF's own environment, which has neither
matplotlib nor TensorFlow, so keep one terminal for `idf.py` and another for the Python tools.

On Linux your user must be allowed to open the serial port: `sudo usermod -aG dialout $USER`, then log out and in.

## Anatomy of the firmware project

```
firmware/
├── components/board/          board_config.h: pins and resistor values, the one file to edit for other hardware
├── components/buzzer/         shared driver, used again by the inference firmware
└── data_logger/
    ├── CMakeLists.txt         project file
    ├── sdkconfig.defaults     the settings that differ from ESP-IDF's defaults
    └── main/
        ├── CMakeLists.txt     lists the source file and the components it needs
        └── data_logger_main.c
```

Three things in these files are specific to this board and worth understanding.

**`sdkconfig.defaults` describes the module.** It sets the target to `esp32s3`, the flash size to 16 MB, and
enables the 8 MB octal PSRAM. `sdkconfig` itself is generated from it on the first build. For a module other
than the N16R8, change `CONFIG_ESPTOOLPY_FLASHSIZE_16MB` to your flash size and delete the three `CONFIG_SPIRAM`
lines if it has no octal PSRAM, in both `firmware/data_logger` and `firmware/inference`. Nothing in the tutorial
needs the PSRAM.

**The console is on the native USB port.** `CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y` sends `printf` output to the
chip's built-in USB serial device, the one that shows up as `/dev/ttyACM0`, and lets the PC send bytes back. No
USB-to-UART adapter is involved. On a development board with two USB sockets, use the one labelled **USB**, not
**UART**: the firmware reads its commands from the native port.

**The build is minimal.** `idf_build_set_property(MINIMAL_BUILD ON)` compiles only the components `main` asks for
in its `REQUIRES` list. The price is that every dependency must be named: leaving out `vfs`, for example, fails
the build with a message that names the missing component.

## Build and flash

```bash
cd firmware/data_logger
```

```bash
idf.py build
```

```bash
idf.py -p /dev/ttyACM0 flash monitor
```

Flashing replaces whatever firmware is on the board. Leave the monitor with `Ctrl+]`. If your port has another
name, pass it with `-p` here and with `--port` to the Python tools.

## Check

The boot log reports the PSRAM, the buzzer beeps once, and lines starting with `D,` scroll past ten times a
second:

```
I (...) data_logger: logging MQ-135 on ADC1 channel 5 at 10 Hz
D,1908,97.54,164.95,49.91
D,2008,97.57,165.00,49.90
```
