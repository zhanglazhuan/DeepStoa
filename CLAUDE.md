# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

DeepStoa is an e-ink flash card device for learners, built on ESP-IDF v5.5.3 (Espressif's IoT Development Framework). The device displays flashcards on an e-ink screen for study/review.

## Build System

This is an ESP-IDF project using CMake. The IDF environment must be sourced before building.

```bash
# Build the project
idf.py build

# Flash to the device
idf.py -p <PORT> flash

# Build, flash, and open serial monitor
idf.py -p <PORT> flash monitor

# Serial monitor only
idf.py -p <PORT> monitor

# Clean build artifacts
idf.py fullclean
```

## Architecture

- **ESP-IDF entry point**: `app_main()` in `main/DeepStoa.c` is the FreeRTOS application entry point — equivalent to `main()` in a standard C program. ESP-IDF initializes hardware and FreeRTOS before calling this.
- **Component model**: ESP-IDF uses a component-based architecture. `main/` is the primary application component. Additional reusable components go in a `components/` directory at the project root.
- **CMake**: Root `CMakeLists.txt` includes the ESP-IDF project boilerplate; `main/CMakeLists.txt` registers source files and include directories for the main component via `idf_component_register`.

## ESP-IDF v5.5.3

Local installation path: `C:/Espressif/frameworks/esp-idf-v5.5.3/`

Standard IDF environment setup on Windows:
```bash
. C:/Espressif/frameworks/esp-idf-v5.5.3/export.sh
```

## Hardware Context

The target hardware includes an e-ink display. Code interacting with the display should account for e-ink characteristics: slow refresh rates, no need for constant refresh (static image persists without power), and support for partial vs. full screen updates to avoid ghosting.
