# m5_midi

MIDI keyboard firmware for the [M5Stack Cardputer](https://docs.m5stack.com/en/core/Cardputer) (ESP32-S3), built with ESP-IDF.

## Project layout

`bsp`, `drv`, `sys`, `app` are grouping directories, not components themselves. Each actual
component lives one level down, named `<layer>_<name>` (e.g. `bsp/bsp_i2c/`), and is wired
into the build via `EXTRA_COMPONENT_DIRS` in the root `CMakeLists.txt`.

```
├── CMakeLists.txt
├── main
│   ├── CMakeLists.txt
│   └── m5_midi_main.c        app_main(), calls into app_core
└── components
    ├── bsp/                  Cardputer board bring-up (power, display, keyboard, speaker, battery, ...)
    │   └── bsp_i2c/          shared I2C master bus init + device registration
    ├── drv/                  peripheral IC drivers (display, keyboard, MIDI transport, ...) - empty for now
    ├── sys/                  system services
    │   └── sys_nvs/          NVS flash init
    └── app/                  MIDI keyboard application logic
        └── app_core/         app_start(), wires up sys/bsp/drv components
```

Note: the peripheral-driver layer directory is named `drv` (not `driver`) to avoid shadowing
ESP-IDF's built-in `driver` component (gpio/i2c/spi/uart/...).

## Build

```
idf.py set-target esp32s3
idf.py build
idf.py -p PORT flash monitor
```
