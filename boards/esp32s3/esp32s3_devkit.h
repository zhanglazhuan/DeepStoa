// boards/esp32s3/esp32s3_devkit.h
// Pin definitions for ESP32-S3 DevKit + GDEM0397T81P e-ink display
// Wiring matches Arduino reference (refer/GDEM0397T81P_Arduino/Display_EPD_W21_spi.h)
//
//   ESP32-S3 DevKit  ───  GDEM0397T81P Module
//   ─────────────────────────────────────────
//   GPIO 11 (MOSI)   ───  SDI (Serial Data In)
//   GPIO 12 (SCK)    ───  SCK (Serial Clock)
//   GPIO 15          ───  CS  (Chip Select)
//   GPIO 6           ───  DC  (Data/Command)
//   GPIO 7           ───  RST (Reset)
//   GPIO 8           ───  BUSY
//   3.3V             ───  VCC
//   GND              ───  GND

#ifndef ESP32S3_DEVKIT_H
#define ESP32S3_DEVKIT_H

#include "hal/gpio_types.h"

// SPI pins (hardware SPI, FSPI/SPI2 host)
#define DEVKIT_PIN_EPD_MOSI     GPIO_NUM_11
#define DEVKIT_PIN_EPD_SCK      GPIO_NUM_12

// Control pins (direct GPIO)
#define DEVKIT_PIN_EPD_CS       GPIO_NUM_15
#define DEVKIT_PIN_EPD_DC       GPIO_NUM_6
#define DEVKIT_PIN_EPD_RST      GPIO_NUM_7
#define DEVKIT_PIN_EPD_BUSY     GPIO_NUM_8

// SPI host peripheral (SPI2 = FSPI, the standard SPI peripheral on ESP32-S3)
#define DEVKIT_EPD_SPI_HOST     SPI2_HOST

// ── Touch controller (FT6336U) ────────────────────────────────────────
#define DEVKIT_TOUCH_I2C_PORT   0            // I2C port 0
#define DEVKIT_PIN_TOUCH_SCL    GPIO_NUM_42
#define DEVKIT_PIN_TOUCH_SDA    GPIO_NUM_41
#define DEVKIT_PIN_TOUCH_INT    GPIO_NUM_4
#define DEVKIT_PIN_TOUCH_RST    GPIO_NUM_5
#define DEVKIT_TOUCH_I2C_ADDR   0x38

#endif // ESP32S3_DEVKIT_H
