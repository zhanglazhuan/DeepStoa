// debug/t_quality/main/t_quality.c
// E-ink image-quality test: GDEM0397T81P via ESP-IDF hardware SPI + direct GPIO
//
// Board: Heltec Wireless Stick V3 (selected via EPD_BOARD_HELTEC_WIRELESS_STICK_V3 in main/CMakeLists.txt)
// Pin definitions: boards/heltec_wireless_stick_v3/heltec_wireless_stick_v3.h
//   GPIO 35=MOSI, 36=SCK, 34=CS, 6=DC, 7=RST, 3=BUSY
// Button: GPIO0 (PRG/BOOT button, active low, internal pull-up)
//
// Each button press cycles the panel through three full-refresh patterns:
//   1. all black
//   2. all white
//   3. 32px checkerboard (chess board)
// After every refresh the panel is put into deep sleep; the image persists.
// Driver shared with debug/t_display (epd_display.c).

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"

#include "esp32s3_devkit.h"
#include "epd_display.h"
#include "ft6336.h"

static const char *TAG = "t_quality";

#define BUTTON_GPIO         GPIO_NUM_0
#define BUTTON_POLL_MS      10
#define BUTTON_DEBOUNCE_MS  50
#define CHECKER_CELL_PX     32
#define TOUCH_POLL_MS       20

typedef enum {
    PATTERN_BLACK = 0,
    PATTERN_WHITE,
    PATTERN_CHECKER,
    PATTERN_COUNT
} pattern_t;

static const char *pattern_name[PATTERN_COUNT] = {
    "black", "white", "checkerboard 32px",
};

// 1 bit per pixel, 1 = white, 0 = black, MSB first, EPD_WIDTH/8 bytes per row.
static uint8_t framebuffer[EPD_ARRAY];

static void fill_checkerboard(uint8_t *buf)
{
    /* GDEM0397T81P RAM is column-major: each column contains
     * EPD_HEIGHT / 8 bytes, with 8 vertical pixels per byte.  A row-major
     * buffer happens to work for solid fills but turns a checkerboard into
     * diagonal/garbled blocks. */
    const int bytes_per_col = EPD_HEIGHT / 8;
    const int bytes_per_cell = CHECKER_CELL_PX / 8;

    for (int x = 0; x < EPD_WIDTH; x++) {
        int cell_col = x / CHECKER_CELL_PX;
        uint8_t *column = buf + x * bytes_per_col;
        for (int byte_index = 0; byte_index < bytes_per_col; byte_index++) {
            int cell_row = byte_index / bytes_per_cell;
            column[byte_index] = ((cell_row + cell_col) & 1) ? 0x00 : 0xFF;
        }
    }
}

static void show_pattern(pattern_t p)
{
    ESP_LOGI(TAG, "Showing pattern %d: %s", p + 1, pattern_name[p]);

    // Panel is in deep sleep after the previous refresh; re-init (HW reset) first.
    EPD_HW_Init();

    switch (p) {
    case PATTERN_BLACK:
        EPD_WhiteScreen_Black();
        break;
    case PATTERN_WHITE:
        EPD_WhiteScreen_White();
        break;
    case PATTERN_CHECKER:
        fill_checkerboard(framebuffer);
        EPD_WhiteScreen_ALL(framebuffer);
        break;
    default:
        break;
    }

    EPD_DeepSleep();
    ESP_LOGI(TAG, "Pattern %s done, panel in deep sleep", pattern_name[p]);
}

static void button_gpio_config(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << BUTTON_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&cfg));
}

// Blocks until a debounced press (high -> low) is detected.
static bool button_poll_press(void)
{
    static bool armed = true;
    int level = gpio_get_level(BUTTON_GPIO);

    if (level != 0) {
        armed = true;
        return false;
    }

    if (!armed) return false;
    armed = false;
    vTaskDelay(pdMS_TO_TICKS(BUTTON_DEBOUNCE_MS));
    return gpio_get_level(BUTTON_GPIO) == 0;
}

void app_main(void)
{
    ESP_LOGI(TAG, "=== t_quality: E-ink Image Quality Test ===");

    ESP_LOGI(TAG, "Step 1: Init GPIO and hardware SPI...");
    epd_gpio_config();
    button_gpio_config();

    ESP_LOGI(TAG, "Step 2: Init FT6336 touch (SDA=%d SCL=%d RST=%d)...",
             DEVKIT_PIN_TOUCH_SDA, DEVKIT_PIN_TOUCH_SCL, DEVKIT_PIN_TOUCH_RST);
    bool touch_ready = ft6336_init(DEVKIT_TOUCH_I2C_PORT,
                                   DEVKIT_PIN_TOUCH_SDA,
                                   DEVKIT_PIN_TOUCH_SCL,
                                   DEVKIT_PIN_TOUCH_RST,
                                   DEVKIT_TOUCH_I2C_ADDR) == ESP_OK;
    ESP_LOGI(TAG, "%s: GPIO, SPI, button and touch initialized",
             touch_ready ? "PASS" : "WARN (touch disabled)");

    pattern_t current = PATTERN_BLACK;
    show_pattern(current);

    ESP_LOGI(TAG, "Press GPIO%d or touch the panel to cycle: black -> white -> checkerboard",
             BUTTON_GPIO);

    bool touch_active = false;
    while (1) {
        bool changed = button_poll_press();

        if (touch_ready) {
            ft6336_touch_data_t touch;
            if (ft6336_read(&touch) == ESP_OK) {
                bool active = touch.count > 0;
                if (active && !touch_active) {
                    ESP_LOGI(TAG, "Touch: count=%u x=%u y=%u",
                             (unsigned)touch.count,
                             (unsigned)touch.points[0].x,
                             (unsigned)touch.points[0].y);
                    changed = true;
                }
                touch_active = active;
            }
        }

        if (changed) {
            current = (current + 1) % PATTERN_COUNT;
            show_pattern(current);
        }

        vTaskDelay(pdMS_TO_TICKS(TOUCH_POLL_MS));
    }
}
