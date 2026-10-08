// DeepStoa V1 pre-production board definitions.
// Source: design/墨水屏-原理图V1.0.pdf.

#ifndef DEEPSTOA_V1_H
#define DEEPSTOA_V1_H

#include <stdint.h>

#include "hal/gpio_types.h"
#include "hal/spi_types.h"

/*
 * AW9523 pin encoding used by the AW9523 driver.
 *
 * Values produced by AW9523_PIN() are not ESP32 GPIO numbers.  Only pass
 * DEEPV1_AW_PIN_* values to AW9523 APIs.
 */
#define AW9523_PIN(port, bit) \
    ((uint8_t)((((uint8_t)(port)) << 3) | ((uint8_t)(bit) & 0x07U)))
#define AW9523_GET_PORT(pin) ((uint8_t)(((uint8_t)(pin)) >> 3))
#define AW9523_GET_BIT(pin)  ((uint8_t)(((uint8_t)(pin)) & 0x07U))

#define AW9523_PIN_P0_0 AW9523_PIN(0U, 0U)
#define AW9523_PIN_P0_1 AW9523_PIN(0U, 1U)
#define AW9523_PIN_P0_2 AW9523_PIN(0U, 2U)
#define AW9523_PIN_P0_3 AW9523_PIN(0U, 3U)
#define AW9523_PIN_P0_4 AW9523_PIN(0U, 4U)
#define AW9523_PIN_P0_5 AW9523_PIN(0U, 5U)
#define AW9523_PIN_P0_6 AW9523_PIN(0U, 6U)
#define AW9523_PIN_P0_7 AW9523_PIN(0U, 7U)
#define AW9523_PIN_P1_0 AW9523_PIN(1U, 0U)
#define AW9523_PIN_P1_1 AW9523_PIN(1U, 1U)
#define AW9523_PIN_P1_2 AW9523_PIN(1U, 2U)
#define AW9523_PIN_P1_3 AW9523_PIN(1U, 3U)
#define AW9523_PIN_P1_4 AW9523_PIN(1U, 4U)
#define AW9523_PIN_P1_5 AW9523_PIN(1U, 5U)
#define AW9523_PIN_P1_6 AW9523_PIN(1U, 6U)
#define AW9523_PIN_P1_7 AW9523_PIN(1U, 7U)

/* Shared I2C bus: AW9523, touch controller and audio codecs. */
#define DEEPV1_I2C_PORT          0U
#define DEEPV1_PIN_I2C_SDA       GPIO_NUM_17
#define DEEPV1_PIN_I2C_SCL       GPIO_NUM_18

/* AW9523BTQR: AD1 = 0, AD0 = 0. */
#define DEEPV1_AW9523_ADDR       0x58U
#define DEEPV1_PIN_IO_RESET      GPIO_NUM_13
#define DEEPV1_PIN_IO_INT        GPIO_NUM_14

/* AW9523 outputs and inputs, named after the schematic nets. */
#define DEEPV1_AW_PIN_EN_POWER   AW9523_PIN_P0_0
#define DEEPV1_AW_PIN_AMP_EN     AW9523_PIN_P0_1
#define DEEPV1_AW_PIN_EN_MOTOR   AW9523_PIN_P1_0
#define DEEPV1_AW_PIN_AP_EN      AW9523_PIN_P1_1
#define DEEPV1_AW_PIN_USB_DETECT AW9523_PIN_P1_2
#define DEEPV1_AW_PIN_CHARGE     AW9523_PIN_P1_3
#define DEEPV1_AW_PIN_TP_RESET   AW9523_PIN_P1_4
#define DEEPV1_AW_PIN_LCD_DC     AW9523_PIN_P1_5
#define DEEPV1_AW_PIN_LCD_RESET  AW9523_PIN_P1_6
#define DEEPV1_AW_PIN_LCD_POWER  AW9523_PIN_P1_7

/* AW9523 direction registers use 1 for input and 0 for output. */
#define DEEPV1_AW_P0_INPUT_MASK  0xFCU
#define DEEPV1_AW_P0_OUTPUT_MASK 0x03U
#define DEEPV1_AW_P1_INPUT_MASK  0x0CU
#define DEEPV1_AW_P1_OUTPUT_MASK 0xF3U

/* Keep every controlled peripheral disabled or held in reset at startup. */
#define DEEPV1_AW_P0_SAFE_OUTPUT 0x00U
#define DEEPV1_AW_P1_SAFE_OUTPUT 0x00U

/* E-paper display: SPI signals are direct GPIOs; control is split. */
#define DEEPV1_EPD_SPI_HOST      SPI2_HOST
#define DEEPV1_PIN_LCD_SDI       GPIO_NUM_10
#define DEEPV1_PIN_LCD_SCLK      GPIO_NUM_11
#define DEEPV1_PIN_LCD_CS        GPIO_NUM_12
#define DEEPV1_PIN_LCD_BUSY      GPIO_NUM_21

#define DEEPV1_PIN_EPD_MOSI      DEEPV1_PIN_LCD_SDI
#define DEEPV1_PIN_EPD_SCK       DEEPV1_PIN_LCD_SCLK
#define DEEPV1_PIN_EPD_CS        DEEPV1_PIN_LCD_CS
#define DEEPV1_PIN_EPD_BUSY      DEEPV1_PIN_LCD_BUSY
#define DEEPV1_AW_PIN_EPD_DC     DEEPV1_AW_PIN_LCD_DC
#define DEEPV1_AW_PIN_EPD_RESET  DEEPV1_AW_PIN_LCD_RESET
#define DEEPV1_AW_PIN_EPD_POWER  DEEPV1_AW_PIN_LCD_POWER

/* FT6336U touch controller; reset is driven through the AW9523. */
#define DEEPV1_TOUCH_I2C_PORT    DEEPV1_I2C_PORT
#define DEEPV1_TOUCH_I2C_ADDR    0x38U
#define DEEPV1_PIN_TOUCH_SDA     DEEPV1_PIN_I2C_SDA
#define DEEPV1_PIN_TOUCH_SCL     DEEPV1_PIN_I2C_SCL
#define DEEPV1_PIN_TOUCH_INT     GPIO_NUM_9
#define DEEPV1_AW_PIN_TOUCH_RST  DEEPV1_AW_PIN_TP_RESET

/* Audio data and clock nets. */
#define DEEPV1_I2S_PORT          0U
#define DEEPV1_PIN_I2S_DIN       GPIO_NUM_1
#define DEEPV1_PIN_ES7210_INT    GPIO_NUM_2
#define DEEPV1_PIN_I2S_DOUT1     GPIO_NUM_5
#define DEEPV1_PIN_I2S_LRCLK     GPIO_NUM_6
#define DEEPV1_PIN_I2S_BCLK      GPIO_NUM_7
#define DEEPV1_PIN_I2S_MCLK      GPIO_NUM_8

/* USB 2.0 data lines. */
#define DEEPV1_PIN_USB_DN        GPIO_NUM_19
#define DEEPV1_PIN_USB_DP        GPIO_NUM_20

/* Four-bit SDMMC interface. */
#define DEEPV1_PIN_SD_DETECT     GPIO_NUM_40
#define DEEPV1_PIN_SD_DAT1       GPIO_NUM_41
#define DEEPV1_PIN_SD_DAT0       GPIO_NUM_42
#define DEEPV1_PIN_SD_CLK        GPIO_NUM_43
#define DEEPV1_PIN_SD_CMD        GPIO_NUM_44
#define DEEPV1_PIN_SD_DAT3       GPIO_NUM_45
#define DEEPV1_PIN_SD_DAT2       GPIO_NUM_46

/* Keys are pulled down and become high when pressed. */
#define DEEPV1_BUTTON_ACTIVE_LEVEL 1U
#define DEEPV1_PIN_BUTTON_VOL_UP   GPIO_NUM_38
#define DEEPV1_PIN_BUTTON_POWER    GPIO_NUM_39
#define DEEPV1_PIN_BUTTON_VOL_DOWN GPIO_NUM_47
#define DEEPV1_PIN_BUTTON_RECORD   GPIO_NUM_48
#define DEEPV1_PIN_WAKEUP_MCU      GPIO_NUM_4
#define DEEPV1_PIN_BOOT            GPIO_NUM_0

/* Battery divider output (BAT_M) for ADC sampling. */
#define DEEPV1_PIN_BATTERY_SENSE GPIO_NUM_3

#endif // DEEPSTOA_V1_H
