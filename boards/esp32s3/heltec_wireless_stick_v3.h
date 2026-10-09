// boards/heltec_wireless_stick_v3/heltec_wireless_stick_v3.h
// Pin definitions for Heltec Wireless Stick V3 (ESP32-S3) + GDEM0397T81P e-ink display
// Ported from the Zephyr devicetree overlay (SPIM3 pinmux + gpio0 control lines).
//
//   Heltec Wireless Stick V3  ───  GDEM0397T81P Module
//   ────────────────────────────────────────────────────
//   GPIO 35 (SPIM3 MOSI)      ───  SDI (Serial Data In)   (idle output-low)
//   GPIO 36 (SPIM3 SCLK)      ───  SCK (Serial Clock)     (see note: Vext)
//   GPIO 37 (SPIM3 MISO)      ───  (unused by display, reserved on the bus)
//   GPIO 34 (SPIM3 CSEL)      ───  CS  (Chip Select, active low)
//   GPIO 6                    ───  DC  (Data/Command, active high)
//   GPIO 7                    ───  RST (Reset, active low)
//   GPIO 3                    ───  BUSY (active high, internal pull-up)
//   3.3V                      ───  VCC
//   GND                       ───  GND
//
// Note: On Heltec V3 boards GPIO 36 is also the Vext power-enable line,
// so driving it as SCLK may conflict with the on-board Vext / VBAT circuit.

#ifndef HELTEC_WIRELESS_STICK_V3_H
#define HELTEC_WIRELESS_STICK_V3_H

#include "hal/gpio_types.h"

// SPI pins (hardware SPI, SPI3 host — Zephyr SPIM3)
#define HELTEC_STICK_PIN_EPD_MOSI   GPIO_NUM_35
#define HELTEC_STICK_PIN_EPD_SCK    GPIO_NUM_36
#define HELTEC_STICK_PIN_EPD_MISO   GPIO_NUM_37   // not needed by the display, -1 if unused

// Control pins (direct GPIO)
#define HELTEC_STICK_PIN_EPD_CS     GPIO_NUM_34   // active low
#define HELTEC_STICK_PIN_EPD_DC     GPIO_NUM_6    // active high
#define HELTEC_STICK_PIN_EPD_RST    GPIO_NUM_7    // active low
#define HELTEC_STICK_PIN_EPD_BUSY   GPIO_NUM_3    // active high, pull-up

// Polarity / pull configuration (mirrors the Zephyr GPIO flags)
#define HELTEC_STICK_EPD_RST_ACTIVE_LEVEL   0     // GPIO_ACTIVE_LOW
#define HELTEC_STICK_EPD_BUSY_ACTIVE_LEVEL  1     // GPIO_ACTIVE_HIGH
#define HELTEC_STICK_EPD_BUSY_PULLUP        1     // GPIO_PULL_UP

// SPI host peripheral (SPI3 = Zephyr SPIM3)
#define HELTEC_STICK_EPD_SPI_HOST   SPI3_HOST

#endif // HELTEC_WIRELESS_STICK_V3_H
