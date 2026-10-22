// debug/t_quality/main/t_quality.c
// E-ink image-quality test: GDEM0397T81P via ESP-IDF hardware SPI + direct GPIO
//
// Board: ESP32-S3 DevKit (pin definitions in boards/esp32s3/esp32s3_devkit.h)
// Button: GPIO0 (PRG/BOOT button, active low, internal pull-up)
//
// Each button press cycles the panel through three full-refresh patterns:
//   1. 32px grid with centered X/Y coordinate labels
//   2. all black
//   3. all white
// After every refresh the panel is put into deep sleep; the image persists.
// Uses the DevKit display driver kept in this test project's main directory.

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_log.h"

#include "esp32s3_devkit.h"
#include "epd_display.h"
#include "ft6336.h"

static const char *TAG = "t_quality";

#define BUTTON_GPIO         GPIO_NUM_0
#define BUTTON_POLL_MS      10
#define BUTTON_DEBOUNCE_MS  50
#define GRID_SPACING_PX     32
#define TOUCH_POLL_MS       20
#define AXIS_X              (EPD_WIDTH / 2)
#define AXIS_Y              (EPD_HEIGHT / 2)

typedef enum {
    PATTERN_GRID = 0,
    PATTERN_BLACK,
    PATTERN_WHITE,
    PATTERN_COUNT
} pattern_t;

static const char *pattern_name[PATTERN_COUNT] = {
    "32px grid", "black", "white",
};

// 1 bit per pixel, 1 = white, 0 = black; columns contain EPD_HEIGHT/8 bytes.
static uint8_t framebuffer[EPD_ARRAY];

static void put_pixel(uint8_t *buf, int x, int y, bool black)
{
    if (x < 0 || x >= EPD_WIDTH || y < 0 || y >= EPD_HEIGHT) return;
    uint8_t *byte = &buf[x * (EPD_HEIGHT / 8) + y / 8];
    uint8_t mask = (uint8_t)(1u << (7 - y % 8));
    if (black) *byte &= (uint8_t)~mask;
    else *byte |= mask;
}

// Five-column digits, one seven-bit row per byte.
static const uint8_t digit_glyphs[][7] = {
    {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E}, // 0
    {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E}, // 1
    {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F}, // 2
    {0x1E, 0x01, 0x01, 0x0E, 0x01, 0x01, 0x1E}, // 3
    {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02}, // 4
    {0x1F, 0x10, 0x10, 0x1E, 0x01, 0x01, 0x1E}, // 5
    {0x0E, 0x10, 0x10, 0x1E, 0x11, 0x11, 0x0E}, // 6
    {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08}, // 7
    {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E}, // 8
    {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x01, 0x0E}, // 9
};

static void draw_number_label(uint8_t *buf, int x, int y, const char *label)
{
    int len = (int)strlen(label);
    // Clear the grid behind each label to keep small digits readable.
    for (int py = y - 1; py <= y + 7; ++py) {
        for (int px = x - 1; px <= x + len * 6 - 1; ++px) {
            put_pixel(buf, px, py, false);
        }
    }
    for (int i = 0; i < len; ++i) {
        int glyph = label[i] - '0';
        for (int row = 0; row < 7; ++row) {
            for (int col = 0; col < 5; ++col) {
                if (digit_glyphs[glyph][row] & (1u << (4 - col))) {
                    put_pixel(buf, x + i * 6 + col, y + row, true);
                }
            }
        }
    }
}

static void draw_axis_number(uint8_t *buf, int x, int y, int value)
{
    char label[4];
    if (value >= 100) {
        label[0] = '0' + value / 100;
        label[1] = '0' + value / 10 % 10;
        label[2] = '0' + value % 10;
        label[3] = '\0';
    } else if (value >= 10) {
        label[0] = '0' + value / 10;
        label[1] = '0' + value % 10;
        label[2] = '\0';
    } else {
        label[0] = '0' + value;
        label[1] = '\0';
    }
    draw_number_label(buf, x, y, label);
}

static void fill_grid(uint8_t *buf)
{
    const int bytes_per_col = EPD_HEIGHT / 8;
    memset(buf, 0xFF, EPD_ARRAY);

    for (int x = 0, line = 1; x < EPD_WIDTH; x += GRID_SPACING_PX, ++line) {
        memset(buf + x * bytes_per_col, 0x00, bytes_per_col);
        if (line % 4 == 0 && x + 1 < EPD_WIDTH) {
            memset(buf + (x + 1) * bytes_per_col, 0x00, bytes_per_col);
        }
    }

    for (int y = 0, line = 1; y < EPD_HEIGHT; y += GRID_SPACING_PX, ++line) {
        uint8_t mask = (uint8_t)~(1u << (7 - (y % 8)));
        for (int x = 0; x < EPD_WIDTH; ++x) {
            buf[x * bytes_per_col + y / 8] &= mask;
            if (line % 4 == 0 && y + 1 < EPD_HEIGHT) {
                buf[x * bytes_per_col + (y + 1) / 8] &=
                    (uint8_t)~(1u << (7 - ((y + 1) % 8)));
            }
        }
    }

    // Show absolute pixel coordinates near the center, without extra axis lines.
    for (int x = 0; x < EPD_WIDTH; x += GRID_SPACING_PX) {
        int digits = x >= 100 ? 3 : x >= 10 ? 2 : 1;
        int label_x = x - digits * 3 + 1;
        draw_axis_number(buf, label_x < 2 ? 2 : label_x, AXIS_Y + 6, x);
    }
    for (int y = 0; y < EPD_HEIGHT; y += GRID_SPACING_PX) {
        int label_y = y - 3;
        draw_axis_number(buf, AXIS_X + 7, label_y < 2 ? 2 : label_y, y);
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
    case PATTERN_GRID:
        fill_grid(framebuffer);
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

/* Keep logging touch coordinates while the main task refreshes the e-ink panel. */
static void touch_poll_task(void *arg)
{
    (void)arg;
    bool touch_active = false;

    while (1) {
        ft6336_touch_data_t touch;
        esp_err_t err = ft6336_read(&touch);
        if (err == ESP_OK) {
            bool active = touch.count > 0 &&
                          touch.points[0].event != FT6336_EVENT_UP &&
                          touch.points[0].event != FT6336_EVENT_NONE;
            if (active && !touch_active) {
                ESP_LOGI(TAG, "Touch click: x=%u y=%u",
                         (unsigned)touch.points[0].x,
                         (unsigned)touch.points[0].y);
            }
            touch_active = active;
        } else if (err == ESP_ERR_TIMEOUT) {
            touch_active = false;
        }
        vTaskDelay(pdMS_TO_TICKS(TOUCH_POLL_MS));
    }
}

static void diagnose_touch_bus(esp_err_t init_error)
{
    ESP_LOGW(TAG, "[TOUCH_DIAG] init=%s (0x%x), SDA=%d SCL=%d RST=%d",
             esp_err_to_name(init_error), init_error,
             gpio_get_level(DEVKIT_PIN_TOUCH_SDA),
             gpio_get_level(DEVKIT_PIN_TOUCH_SCL),
             gpio_get_level(DEVKIT_PIN_TOUCH_RST));

    i2c_master_bus_handle_t bus;
    esp_err_t err = i2c_master_get_bus_handle(DEVKIT_TOUCH_I2C_PORT, &bus);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "[TOUCH_DIAG] bus handle: %s", esp_err_to_name(err));
        return;
    }

    err = i2c_master_probe(bus, DEVKIT_TOUCH_I2C_ADDR, 100);
    ESP_LOGW(TAG, "[TOUCH_DIAG] probe 0x%02x: %s",
             DEVKIT_TOUCH_I2C_ADDR, esp_err_to_name(err));
    if (err == ESP_OK) return;

    for (uint8_t addr = 0x08; addr <= 0x77; ++addr) {
        if (addr == DEVKIT_TOUCH_I2C_ADDR) continue;
        if (i2c_master_probe(bus, addr, 20) == ESP_OK) {
            ESP_LOGW(TAG, "[TOUCH_DIAG] device found at 0x%02x", addr);
        }
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "=== t_quality: E-ink Image Quality Test ===");

    ESP_LOGI(TAG, "Step 1: Init GPIO and hardware SPI...");
    epd_gpio_config();
    button_gpio_config();

    ESP_LOGI(TAG, "Step 2: Init FT6336 touch (SDA=%d SCL=%d RST=%d)...",
             DEVKIT_PIN_TOUCH_SDA, DEVKIT_PIN_TOUCH_SCL, DEVKIT_PIN_TOUCH_RST);
    esp_err_t touch_err = ft6336_init(DEVKIT_TOUCH_I2C_PORT,
                                      DEVKIT_PIN_TOUCH_SDA,
                                      DEVKIT_PIN_TOUCH_SCL,
                                      DEVKIT_PIN_TOUCH_RST,
                                      DEVKIT_TOUCH_I2C_ADDR);
    bool touch_ready = touch_err == ESP_OK;
    if (!touch_ready) diagnose_touch_bus(touch_err);
    if (touch_ready && xTaskCreate(touch_poll_task, "quality_touch", 3072,
                                   NULL, 5, NULL) != pdPASS) {
        ESP_LOGW(TAG, "Could not start touch coordinate logger");
        touch_ready = false;
    }
    ESP_LOGI(TAG, "%s: GPIO, SPI, button and touch initialized",
             touch_ready ? "PASS" : "WARN (touch disabled)");

    pattern_t current = PATTERN_GRID;
    show_pattern(current);

    ESP_LOGI(TAG, "Press GPIO%d to cycle: grid -> black -> white",
             BUTTON_GPIO);

    while (1) {
        if (button_poll_press()) {
            current = (current + 1) % PATTERN_COUNT;
            show_pattern(current);
        }

        vTaskDelay(pdMS_TO_TICKS(BUTTON_POLL_MS));
    }
}
