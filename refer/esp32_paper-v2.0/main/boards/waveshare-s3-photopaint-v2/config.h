#ifndef _BOARD_CONFIG_H_
#define _BOARD_CONFIG_H_

#include <driver/gpio.h>

// Audio sample rates
#define AUDIO_INPUT_SAMPLE_RATE  16000
#define AUDIO_OUTPUT_SAMPLE_RATE 16000
#define AUDIO_INPUT_REFERENCE    true

// Audio I2S pins (ES7210 ADC)
#define AUDIO_I2S_GPIO_MCLK GPIO_NUM_7
#define AUDIO_I2S_GPIO_WS   GPIO_NUM_5
#define AUDIO_I2S_GPIO_BCLK GPIO_NUM_6
#define AUDIO_I2S_GPIO_DIN  GPIO_NUM_8
#define AUDIO_I2S_GPIO_DOUT GPIO_NUM_4

// V1.0 schematic shared I2C bus: SDA=GPIO17, SCL=GPIO18.
// Devices: AW9523B, ES8311, ES7210, AHT20 and QMI8658A.
#define AUDIO_I2C_SDA_PIN    GPIO_NUM_17
#define AUDIO_I2C_SCL_PIN    GPIO_NUM_18
#define AUDIO_I2C_PORT       I2C_NUM_0

// Camera SCCB and the touch connector also use IIC_DATA/IIC_CLK.
#define EXT_I2C_SDA_PIN      AUDIO_I2C_SDA_PIN
#define EXT_I2C_SCL_PIN      AUDIO_I2C_SCL_PIN
#define EXT_I2C_PORT         AUDIO_I2C_PORT

// esp_codec_dev expects 8-bit addresses and shifts them right once internally.
// On the wire: ES8311=0x18, ES7210=0x40 (7-bit).
#define AUDIO_CODEC_ES8311_ADDR  0x30
#define AUDIO_CODEC_ES7210_ADDR  0x80
// Software pin swapping is disabled by default: all devices use SDA17/SCL18.
// CONFIG_PHOTOPAINT_ES7210_SWAPPED_I2C is an optional assembly workaround.
#define ENV_SENSOR_AHT20_ADDR    0x38
#define IMU_QMI8658_ADDR         0x6B  // Verified on this board: WHO_AM_I=0x05

// ES7210 MIC configuration (TDM mode)
#define AUDIO_CODEC_ES7210_MIC_SELECT  (ES7210_SEL_MIC1 | ES7210_SEL_MIC2)

// AW9523 (AD0=AD1=GND)
#define IO_EXPANDER_I2C_ADDR     0x58
#define IO_EXPANDER_INT_GPIO     GPIO_NUM_3
#define IO_EXPANDER_RESET_GPIO   GPIO_NUM_46

// IO expander pin assignments
#define IO_EXP_AUDIOCTR     (1U << 0)   // P0.0 AMP_EN
#define IO_EXP_KEY_UP       (1U << 1)   // P0.1 UP
#define IO_EXP_KEY_DOWN     (1U << 2)   // P0.2 DOWN
#define IO_EXP_KEY_FUNC     (1U << 3)   // P0.3 Vol
#define IO_EXP_POWER_KEY    (1U << 4)   // P0.4 ON/OFF, active-high key input
#define IO_EXP_LCD_INT      (1U << 5)   // P0.5 LCD_INT
#define IO_EXP_EPD_MOSI     (1U << 6)   // P0.6 LCD_SDI
#define IO_EXP_EPD_SCLK     (1U << 7)   // P0.7 LCD_SCLK
#define IO_EXP_IMU_INT1     (1U << 8)   // P1.0, schematic net AXP_INT2 -> QMI8658 INT2
#define IO_EXP_IMU_INT2     (1U << 9)   // P1.1, schematic net AXP_INT1 -> QMI8658 INT1
#define IO_EXP_ES7210_INT   (1U << 10)  // P1.2 ES7210_INT3
#define IO_EXP_AP_EN        (1U << 11)  // P1.3 AP_EN
#define IO_EXP_EPD_CS       (1U << 12)  // P1.4 LCD_CS
#define IO_EXP_EPD_DC       (1U << 13)  // P1.5 LCD_D/C
#define IO_EXP_EPD_BUSY     (1U << 14)  // P1.6 LCD_BUSY
#define IO_EXP_EPD_POWER    (1U << 15)  // P1.7 EN_Power

// IO expander direction masks
#define DRV_IO_EXP_OUTPUT_MASK \
    (IO_EXP_AUDIOCTR | \
     IO_EXP_AP_EN | IO_EXP_EPD_CS | IO_EXP_EPD_DC | IO_EXP_EPD_POWER)

#define DRV_IO_EXP_INPUT_MASK \
    (IO_EXP_KEY_UP | IO_EXP_KEY_DOWN | IO_EXP_KEY_FUNC | IO_EXP_POWER_KEY | IO_EXP_LCD_INT | \
     IO_EXP_IMU_INT1 | IO_EXP_IMU_INT2 | IO_EXP_ES7210_INT | IO_EXP_EPD_BUSY | \
     IO_EXP_EPD_MOSI | IO_EXP_EPD_SCLK)

// E-paper signals are connected to AW9523, not native ESP32 GPIOs.
#define EPD_MOSI_PIN  IO_EXP_EPD_MOSI
#define EPD_SCLK_PIN  IO_EXP_EPD_SCLK
#define EPD_CS_PIN    IO_EXP_EPD_CS
#define EPD_DC_PIN    IO_EXP_EPD_DC
#define EPD_BUSY_PIN  IO_EXP_EPD_BUSY
#define EPD_POWER_PIN IO_EXP_EPD_POWER

#define EPD_WIDTH   800
#define EPD_HEIGHT  480

// SD Card (1-bit SDMMC)
#define SDMMC_CLK_PIN   GPIO_NUM_1
#define SDMMC_CMD_PIN   GPIO_NUM_10
#define SDMMC_D0_PIN    GPIO_NUM_2
#define SDMMC_BUS_WIDTH 1

// Buttons
#define BOOT_BUTTON_GPIO        GPIO_NUM_0
#define VOLUME_UP_BUTTON_GPIO   GPIO_NUM_NC
#define VOLUME_DOWN_BUTTON_GPIO GPIO_NUM_NC

// LEDs
#define CODE_LIGHT_GPIO GPIO_NUM_NC
#define WARM_LIGHT_GPIO GPIO_NUM_NC
#define BUILTIN_LED_GPIO GPIO_NUM_NC

// External interrupt
#define EXT_INT_GPIO IO_EXPANDER_INT_GPIO

// Camera (OV2640)
#define CAMERA_PIN_D0     GPIO_NUM_21  // CAMERA_Y2
#define CAMERA_PIN_D1     GPIO_NUM_13  // CAMERA_Y3
#define CAMERA_PIN_D2     GPIO_NUM_12  // CAMERA_Y4
#define CAMERA_PIN_D3     GPIO_NUM_14  // CAMERA_Y5
#define CAMERA_PIN_D4     GPIO_NUM_47  // CAMERA_Y6
#define CAMERA_PIN_D5     GPIO_NUM_45  // CAMERA_Y7
#define CAMERA_PIN_D6     GPIO_NUM_38  // CAMERA_Y8
#define CAMERA_PIN_D7     GPIO_NUM_40  // CAMERA_Y9
#define CAMERA_PIN_XCLK   GPIO_NUM_39
#define CAMERA_PIN_PCLK   GPIO_NUM_48
#define CAMERA_PIN_VSYNC  GPIO_NUM_42
#define CAMERA_PIN_HREF   GPIO_NUM_41
#define CAMERA_PIN_SIOD   EXT_I2C_SDA_PIN   // SCCB via EXT_I2C
#define CAMERA_PIN_SIOC   EXT_I2C_SCL_PIN   // SCCB via EXT_I2C
#define CAMERA_PIN_PWDN   -1  // Controlled via IO expander P13
#define CAMERA_PIN_RESET  -1  // Not connected
#define XCLK_FREQ_HZ      20000000

// YRD04MDW0670B: FT6336U, fitted controller observed at 7-bit address 0x2E.
// J9: 1=GND, 2=3V3, 3=NC (module RST), 4=INT, 5=SDA, 6=SCL.
#define TOUCH_I2C_SDA_PIN    EXT_I2C_SDA_PIN
#define TOUCH_I2C_SCL_PIN    EXT_I2C_SCL_PIN
#define TOUCH_INT_PIN        -1  // LCD_INT is AW9523 P0.5
#define TOUCH_I2C_ADDR       0x2E
// Gesture directions in the portrait UI; confirm these on the fitted panel.
#define TOUCH_SWAP_XY        false
#define TOUCH_INVERT_X       false
#define TOUCH_INVERT_Y       false

// Display configuration
#define DISPLAY_WIDTH   800
#define DISPLAY_HEIGHT  480
#define DISPLAY_MIRROR_X false
#define DISPLAY_MIRROR_Y false
#define DISPLAY_SWAP_XY false
#define DISPLAY_OFFSET_X 0
#define DISPLAY_OFFSET_Y 0
#define DISPLAY_BACKLIGHT_PIN GPIO_NUM_NC
#define DISPLAY_BACKLIGHT_OUTPUT_INVERT false

#endif // _BOARD_CONFIG_H_
