// drivers/ft6336/ft6336.c
// FT6336U I2C capacitive touch controller driver
//
// Reference register map from FocalTech FT6x36 datasheet.
// Uses ESP-IDF v5.x i2c_master API.

#include "ft6336.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "ft6336";

// ─── Register map ──────────────────────────────────────────────────────

#define FT6336_REG_DEVIDE_MODE      0x00   // device mode / chip ID
#define FT6336_REG_GEST_ID          0x01   // gesture ID
#define FT6336_REG_TD_STATUS        0x02   // touch status (count in bits [3:0])
#define FT6336_REG_TOUCH1_XH        0x03   // touch 1 X high byte
#define FT6336_REG_TOUCH1_XL        0x04
#define FT6336_REG_TOUCH1_YH        0x05
#define FT6336_REG_TOUCH1_YL        0x06
#define FT6336_REG_TOUCH1_WEIGHT    0x07
#define FT6336_REG_TOUCH1_MISC      0x08
#define FT6336_REG_TOUCH2_XH        0x09
#define FT6336_REG_TOUCH2_XL        0x0A
#define FT6336_REG_TOUCH2_YH        0x0B
#define FT6336_REG_TOUCH2_YL        0x0C
#define FT6336_REG_TOUCH2_WEIGHT    0x0D
#define FT6336_REG_TOUCH2_MISC      0x0E

#define FT6336_TOUCH_REPORT_LEN     (FT6336_REG_TOUCH2_MISC - FT6336_REG_GEST_ID + 1)
#define FT6336_REPORT_INDEX(reg)    ((reg) - FT6336_REG_GEST_ID)

#define FT6336_REG_THRESHOLD        0x80   // touch threshold
#define FT6336_REG_SAMPLE_RATE      0x88   // report rate
#define FT6336_REG_POWER_MODE       0xA4   // power mode (0=normal, 3=sleep)
#define FT6336_REG_CHIP_ID          0xA8   // firmware / library version

// ─── Internal state ────────────────────────────────────────────────────

static i2c_master_bus_handle_t s_i2c_bus = NULL;
static i2c_master_dev_handle_t s_i2c_dev = NULL;
static gpio_num_t s_rst_pin = GPIO_NUM_NC;

// ─── I2C helpers ───────────────────────────────────────────────────────

static esp_err_t ft6336_read_regs(uint8_t start_reg, uint8_t *data, size_t len)
{
    return i2c_master_transmit_receive(s_i2c_dev, &start_reg, 1, data, len, 100);
}

static esp_err_t ft6336_write_reg(uint8_t reg, uint8_t value)
{
    uint8_t buf[2] = { reg, value };
    return i2c_master_transmit(s_i2c_dev, buf, 2, 100);
}

// ─── Hardware reset ────────────────────────────────────────────────────

static void ft6336_hw_reset(void)
{
    if (s_rst_pin == GPIO_NUM_NC) {
        ESP_LOGW(TAG, "No RST pin configured, skipping hardware reset");
        return;
    }

    gpio_set_level(s_rst_pin, 0);
    vTaskDelay(pdMS_TO_TICKS(10));      // hold low ≥5ms
    gpio_set_level(s_rst_pin, 1);
    vTaskDelay(pdMS_TO_TICKS(350));     // wait ≥300ms after reset
}

// ─── Parse touch data ──────────────────────────────────────────────────

static void ft6336_parse_point(const uint8_t *regs, int offset,
                               ft6336_touch_point_t *point)
{
    uint8_t xh = regs[offset];
    uint8_t xl = regs[offset + 1];
    uint8_t yh = regs[offset + 2];
    uint8_t yl = regs[offset + 3];

    point->x = ((uint16_t)(xh & 0x0F) << 8) | xl;
    point->y = ((uint16_t)(yh & 0x0F) << 8) | yl;
    point->event = (ft6336_event_t)((xh >> 6) & 0x03);
    point->weight = regs[offset + 4];
    point->area = (regs[offset + 5] >> 4) & 0x0F;
    point->id = (yh >> 4) & 0x0F;
}

static bool ft6336_point_is_active(const ft6336_touch_point_t *point)
{
    return point->event == FT6336_EVENT_DOWN ||
           point->event == FT6336_EVENT_CONTACT;
}

// ─── Public API ────────────────────────────────────────────────────────

esp_err_t ft6336_init(i2c_port_t i2c_port, gpio_num_t sda_pin, gpio_num_t scl_pin,
                      gpio_num_t rst_pin, uint8_t i2c_addr)
{
    ESP_LOGI(TAG, "Initializing FT6336 on I2C port %d (addr 0x%02X)...",
             i2c_port, i2c_addr);

    s_rst_pin = rst_pin;

    // ── RST pin setup ──────────────────────────────────────────────
    if (rst_pin != GPIO_NUM_NC) {
        gpio_config_t rst_conf = {
            .pin_bit_mask = (1ULL << rst_pin),
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_ENABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        gpio_config(&rst_conf);
        gpio_set_level(rst_pin, 1);
    }

    // ── I2C bus ────────────────────────────────────────────────────
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = i2c_port,
        .sda_io_num = sda_pin,
        .scl_io_num = scl_pin,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags = {
            .enable_internal_pullup = true,
        },
    };
    esp_err_t ret = i2c_new_master_bus(&bus_cfg, &s_i2c_bus);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create I2C bus: %d", ret);
        return ret;
    }

    // ── I2C device ─────────────────────────────────────────────────
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = i2c_addr,
        .scl_speed_hz = 400 * 1000,   // FT6336 supports up to 400 kHz
    };
    ret = i2c_master_bus_add_device(s_i2c_bus, &dev_cfg, &s_i2c_dev);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add I2C device: %d", ret);
        return ret;
    }

    // ── Hardware reset ─────────────────────────────────────────────
    ft6336_hw_reset();

    // ── Verify chip ────────────────────────────────────────────────
    uint8_t chip_id;
    ret = ft6336_get_chip_id(&chip_id);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read chip ID: %d", ret);
        return ret;
    }
    ESP_LOGI(TAG, "Chip ID: 0x%02X", chip_id);

    // ── Default configuration ──────────────────────────────────────
    // Set to normal operating mode (wake from any previous sleep)
    ft6336_write_reg(FT6336_REG_POWER_MODE, 0x00);
    vTaskDelay(pdMS_TO_TICKS(50));

    // Set default threshold
    ft6336_write_reg(FT6336_REG_THRESHOLD, 30);

    ESP_LOGI(TAG, "FT6336 initialized successfully");
    return ESP_OK;
}

esp_err_t ft6336_init_shared(void *bus, gpio_num_t rst_pin, uint8_t i2c_addr)
{
    if (!bus) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Initializing FT6336 on shared I2C bus (addr 0x%02X)...", i2c_addr);

    s_i2c_bus = (i2c_master_bus_handle_t)bus;
    s_rst_pin = rst_pin;

    // ── RST pin setup ──────────────────────────────────────────────
    if (rst_pin != GPIO_NUM_NC) {
        gpio_config_t rst_conf = {
            .pin_bit_mask = (1ULL << rst_pin),
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_ENABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        gpio_config(&rst_conf);
        gpio_set_level(rst_pin, 1);
    }

    // ── Add device to existing bus ─────────────────────────────────
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = i2c_addr,
        .scl_speed_hz = 400 * 1000,
    };
    esp_err_t ret = i2c_master_bus_add_device(s_i2c_bus, &dev_cfg, &s_i2c_dev);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add I2C device to shared bus: %d", ret);
        return ret;
    }

    // ── Hardware reset ─────────────────────────────────────────────
    ft6336_hw_reset();

    // ── Default configuration ──────────────────────────────────────
    ft6336_write_reg(FT6336_REG_POWER_MODE, 0x00);
    vTaskDelay(pdMS_TO_TICKS(50));
    ft6336_write_reg(FT6336_REG_THRESHOLD, 30);

    ESP_LOGI(TAG, "FT6336 initialized on shared bus");
    return ESP_OK;
}

esp_err_t ft6336_get_chip_id(uint8_t *chip_id)
{
    return ft6336_read_regs(FT6336_REG_CHIP_ID, chip_id, 1);
}

esp_err_t ft6336_read(ft6336_touch_data_t *data)
{
    if (!data) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(data, 0, sizeof(*data));

    // Read the status and both complete point records in one I2C transaction so
    // coordinates from simultaneous contacts belong to the same controller frame.
    uint8_t regs[FT6336_TOUCH_REPORT_LEN];
    esp_err_t ret = ft6336_read_regs(FT6336_REG_GEST_ID, regs, sizeof(regs));
    if (ret != ESP_OK) {
        return ret;
    }

    data->gesture_id = regs[FT6336_REPORT_INDEX(FT6336_REG_GEST_ID)];
    uint8_t reported_count = regs[FT6336_REPORT_INDEX(FT6336_REG_TD_STATUS)] & 0x0F;

    if (reported_count > FT6336_MAX_TOUCH_POINTS) {
        ESP_LOGW(TAG, "Invalid touch count reported: %u", (unsigned)reported_count);
        return ESP_ERR_INVALID_RESPONSE;
    }

    const uint8_t point_offsets[FT6336_MAX_TOUCH_POINTS] = {
        FT6336_REPORT_INDEX(FT6336_REG_TOUCH1_XH),
        FT6336_REPORT_INDEX(FT6336_REG_TOUCH2_XH),
    };

    for (uint8_t i = 0; i < reported_count; i++) {
        ft6336_touch_point_t point;
        ft6336_parse_point(regs, point_offsets[i], &point);
        if (ft6336_point_is_active(&point)) {
            data->points[data->count++] = point;
        }
    }

    return ESP_OK;
}

esp_err_t ft6336_sleep(bool enable)
{
    uint8_t mode = enable ? 0x03 : 0x00;
    return ft6336_write_reg(FT6336_REG_POWER_MODE, mode);
}
