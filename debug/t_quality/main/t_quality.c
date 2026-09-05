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

#include "epd_display.h"

static const char *TAG = "t_quality";

#define BUTTON_GPIO         GPIO_NUM_0
#define BUTTON_POLL_MS      10
#define BUTTON_DEBOUNCE_MS  50
#define CHECKER_CELL_PX     32

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
    const int bytes_per_row = EPD_WIDTH / 8;
    const int bytes_per_cell = CHECKER_CELL_PX / 8;  // 32px = 4 bytes, so cells are byte-aligned

    for (int y = 0; y < EPD_HEIGHT; y++) {
        int cell_row = y / CHECKER_CELL_PX;
        uint8_t *row = buf + y * bytes_per_row;
        for (int b = 0; b < bytes_per_row; b++) {
            int cell_col = b / bytes_per_cell;
            row[b] = ((cell_row + cell_col) & 1) ? 0x00 : 0xFF;
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
static void button_wait_press(void)
{
    // Wait for release first so a held button does not auto-repeat.
    while (gpio_get_level(BUTTON_GPIO) == 0) {
        vTaskDelay(pdMS_TO_TICKS(BUTTON_POLL_MS));
    }

    while (1) {
        if (gpio_get_level(BUTTON_GPIO) == 0) {
            vTaskDelay(pdMS_TO_TICKS(BUTTON_DEBOUNCE_MS));
            if (gpio_get_level(BUTTON_GPIO) == 0) {
                return;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(BUTTON_POLL_MS));
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "=== t_quality: E-ink Image Quality Test ===");

    ESP_LOGI(TAG, "Step 1: Init GPIO and hardware SPI...");
    epd_gpio_config();
    button_gpio_config();
    ESP_LOGI(TAG, "PASS: GPIO, SPI and button (GPIO%d) initialized", BUTTON_GPIO);

    pattern_t current = PATTERN_BLACK;
    show_pattern(current);

    ESP_LOGI(TAG, "Press the GPIO%d button to cycle: black -> white -> checkerboard", BUTTON_GPIO);

    while (1) {
        button_wait_press();
        current = (current + 1) % PATTERN_COUNT;
        show_pattern(current);
    }
}
