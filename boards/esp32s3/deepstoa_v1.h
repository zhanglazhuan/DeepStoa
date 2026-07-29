// boards/esp32s3/deepstoa_v1.h
// Pin definitions for DeepStoa V1 development board (ESP32S3 + AW9523BTQR)

#ifndef DEEPSTOA_V1_H
#define DEEPSTOA_V1_H

#include "driver/gpio.h"

// AW9523 Pin encoding: (port << 3) | bit
// port 0 → P0, port 1 → P1; bit 0-7
#define AW9523_PIN(port, bit)   (((uint8_t)(port) << 3) | ((uint8_t)(bit) & 0x07))
#define AW9523_GET_PORT(pin)    ((pin) >> 3)
#define AW9523_GET_BIT(pin)     ((pin) & 0x07)

// Predefined AW9523 pins (for convenience)
#define AW9523_PIN_P0_0  AW9523_PIN(0, 0)
#define AW9523_PIN_P0_1  AW9523_PIN(0, 1)
#define AW9523_PIN_P0_2  AW9523_PIN(0, 2)
#define AW9523_PIN_P0_3  AW9523_PIN(0, 3)
#define AW9523_PIN_P0_4  AW9523_PIN(0, 4)
#define AW9523_PIN_P0_5  AW9523_PIN(0, 5)
#define AW9523_PIN_P0_6  AW9523_PIN(0, 6)
#define AW9523_PIN_P0_7  AW9523_PIN(0, 7)
#define AW9523_PIN_P1_0  AW9523_PIN(1, 0)
#define AW9523_PIN_P1_1  AW9523_PIN(1, 1)
#define AW9523_PIN_P1_2  AW9523_PIN(1, 2)
#define AW9523_PIN_P1_3  AW9523_PIN(1, 3)
#define AW9523_PIN_P1_4  AW9523_PIN(1, 4)
#define AW9523_PIN_P1_5  AW9523_PIN(1, 5)
#define AW9523_PIN_P1_6  AW9523_PIN(1, 6)
#define AW9523_PIN_P1_7  AW9523_PIN(1, 7)

// I2C (AW9523 communication)
#define DEEPV1_I2C_PORT         0
#define DEEPV1_PIN_I2C_SCL      GPIO_NUM_18
#define DEEPV1_PIN_I2C_SDA      GPIO_NUM_17
#define DEEPV1_PIN_IO_INT       GPIO_NUM_3
#define DEEPV1_PIN_IO_RESET     GPIO_NUM_46

// AW9523 I2C address (7-bit, AD1=0 AD0=0)
#define DEEPV1_AW9523_ADDR      0x58

// LCD pins (mapped to AW9523 ports)
#define DEEPV1_PIN_LCD_CS       AW9523_PIN_P1_4
#define DEEPV1_PIN_LCD_DC       AW9523_PIN_P1_5
#define DEEPV1_PIN_LCD_BUSY     AW9523_PIN_P1_6
#define DEEPV1_PIN_LCD_INT      AW9523_PIN_P0_5
#define DEEPV1_PIN_LCD_SDI      AW9523_PIN_P0_6
#define DEEPV1_PIN_LCD_SCLK     AW9523_PIN_P0_7

#endif // DEEPSTOA_V1_H
