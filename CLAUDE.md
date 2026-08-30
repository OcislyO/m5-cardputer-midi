# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

MIDI keyboard firmware for the [M5Stack Cardputer](https://docs.m5stack.com/en/core/Cardputer) (ESP32-S3), built with ESP-IDF (>= 5.0). The Cardputer's own physical keyboard is played like a piano; app_synth renders the notes to audio via the onboard ES8311 codec, and a ST7789 LCD shows UI.

## Build / flash

```
idf.py set-target esp32s3
idf.py build
idf.py -p PORT flash monitor
```

There is no test suite and no CI config in this repo — verification is build success plus on-device behavior. `idf.py build` (and `idf.py size`) is the primary correctness signal available without hardware. ESP-IDF is installed at `~/esp32/esp-idf` (see `.vscode/settings.json`).

## Architecture: layered components

Every component lives at `components/<layer>/<layer>_<name>/` and is named `<layer>_<name>` (e.g. `bsp_i2c`, `sys_dsp`, `app_synth`). `bsp`, `drv`, `sys`, `app` themselves are *not* components — they're grouping directories registered via `EXTRA_COMPONENT_DIRS` in the root `CMakeLists.txt` so their children get picked up individually. The layers, bottom to top:

- **bsp/** — Cardputer board bring-up: shared bus/peripheral init (`bsp_i2c`, `bsp_spi`, `bsp_gpio`, `bsp_adc`, `bsp_i2s`). Other layers pull a bus handle from here instead of configuring peripherals themselves.
- **drv/** — one component per peripheral IC (`drv_st7789` display, `drv_es8311` audio codec, `drv_tca8418` keyboard matrix controller, `drv_bmi270` IMU). Named `drv`, not `driver`, specifically to avoid shadowing ESP-IDF's built-in `driver` component.
- **sys/** — system services built on bsp/drv: `sys_dsp` (display/UI object tree + renderer), `sys_audio` (audio output), `sys_kbd` (keyboard scanning), `sys_bat` (battery), `sys_nvs` (flash init).
- **app/** — MIDI keyboard application logic: `app_core` (wires everything up), `app_midi` (MIDI event bus + keyboard-to-note mapping), `app_synth` (synth engine), `app_ui`.

A component only `REQUIRES` what it actually calls (see each `CMakeLists.txt`); when adding a call to another component's API, add it to `REQUIRES`/`PRIV_REQUIRES` too. Init functions across all layers are idempotent (safe to call more than once — they check a static handle/flag and early-return `ESP_OK`), which lets multiple call sites depend on the same subsystem without coordinating ordering by hand.

`main/m5_midi_main.c` just calls `app_start()` (in `app_core.c`) and idles. `app_start()` is the single source of truth for boot order: bsp → sys_dsp/sys_bat/sys_audio → app_midi_bus → app_midi_event/app_ui/app_synth.

### MIDI event bus (`app_midi`)

`app_midi_bus` is a small pub/sub: fixed-size table of subscriber `QueueHandle_t`s (`APP_MIDI_BUS_MAX_SUBSCRIBERS`, currently 4), `app_midi_bus_send()` fans an `app_midi_event_t` out to every subscribed queue non-blocking (drops + logs a warning on a full queue rather than blocking the sender). `app_midi_event.c` is the producer: it reads `sys_kbd` raw row/col events and maps two physical keyboard rows to piano notes via `app_midi_kbd_map`. `app_synth` is a consumer via its own subscription — producer and consumer(s) don't know about each other, only about the bus, so new producers (e.g. a future USB-MIDI IN) or consumers can be added independently.

### Synth engine (`app_synth`)

Two tasks: `app_synth_task` consumes MIDI events off the bus and allocates/releases voices (`app_synth_voice_on/off`) tracked in `note_map[channel][note]`; `app_synth_engine_task` runs the audio render loop — per-sample, per-voice envelope state machine (attack/decay/sustain/release) plus wavetable oscillator lookup (`app_synth_wavetable_sample`, precomputed sine/triangle/saw/square tables) — and pushes mixed frames to `sys_audio`. Voices belong to tracks (`app_synth_track`), which hold shared envelope/oscillator settings for a channel.

### Display (`sys_dsp`)

A retained-mode UI: an object tree (`sys_dsp_obj.c`, `ui_obj_t`, parent-relative `rect_t`) rendered lazily via a dirty-rect list (`sys_dsp_dirty.c`) coalesced and flushed by a dedicated render task (`sys_dsp_render.c`) over `drv_st7789`. Panel is 240x135; `sys_dsp_send_buff` caps one SPI burst at `SYS_DSP_MAX_RENDER_PIXELS` pixels so a single render pass doesn't starve other tasks. `sys_dsp_priv.h` (not under `include/`, not part of the public API) holds the rect math and cross-TU state shared between the three source files.

## Conventions to follow

- Board-specific pin/bus config lives in each component's `Kconfig` (`CONFIG_BSP_I2C_SDA_GPIO`, etc.), not hardcoded in `.c` files.
- Public API goes in `include/<component>.h`; internal cross-TU state for a multi-file component goes in an unexported `<component>_priv.h` next to the `.c` files (see `sys_dsp_priv.h`).
- New peripheral drivers go under `drv/`, new board-level bring-up under `bsp/`, new system services under `sys/`, new application logic under `app/` — match the existing `<layer>_<name>` naming and directory shape rather than introducing a new grouping.
