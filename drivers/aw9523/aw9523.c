// drivers/aw9523/aw9523.c
// AW9523BTQR I2C GPIO Expander Driver Implementation
// Aligned with reference AW9523B driver (refer/aw9523b).
// Uses new i2c_master API (driver/i2c_master.h).

#include "aw9523.h"
#include "deepstoa_v1.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "aw9523";

// Register map — matches AW9523B datasheet
#define AW9523_REG_INPUT_P0     0x00
#define AW9523_REG_INPUT_P1     0x01
#define AW9523_REG_OUTPUT_P0    0x02
#define AW9523_REG_OUTPUT_P1    0x03
#define AW9523_REG_DIR_P0       0x04
#define AW9523_REG_DIR_P1       0x05
#define AW9523_REG_INTR_P0      0x06
#define AW9523_REG_INTR_P1      0x07
#define AW9523_REG_ID           0x10
#define AW9523_REG_CTL_P0       0x11   // GCR: global control register
#define AW9523_REG_LEDMODE_P0   0x12
#define AW9523_REG_LEDMODE_P1   0x13
#define AW9523_REG_RESET        0x7F

static i2c_master_bus_handle_t g_i2c_bus = NULL;
static i2c_master_dev_handle_t g_i2c_dev = NULL;
static uint8_t g_out_cache[2];  // P0, P1 output register cache
static uint8_t g_dir_cache[2];  // P0, P1 direction register cache

static esp_err_t aw9523_read_reg(uint8_t reg, uint8_t *data)
{
    return i2c_master_transmit_receive(g_i2c_dev, &reg, 1, data, 1, 100);
}

static esp_err_t aw9523_write_reg(uint8_t reg, uint8_t data)
{
    uint8_t buf[2] = {reg, data};
    return i2c_master_transmit(g_i2c_dev, buf, 2, 100);
}

// Read-modify-write a register
static esp_err_t aw9523_update_reg(uint8_t reg, uint8_t mask, uint8_t value)
{
    uint8_t cur;
    esp_err_t ret = aw9523_read_reg(reg, &cur);
    if (ret != ESP_OK) return ret;
    cur = (cur & ~mask) | (value & mask);
    return aw9523_write_reg(reg, cur);
}

esp_err_t aw9523_init(uint8_t i2c_port, gpio_num_t sda_pin, gpio_num_t scl_pin,
                      gpio_num_t rst_pin, uint8_t i2c_addr)
{
    // --- I2C master bus init ---
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = (i2c_port_t)i2c_port,
        .sda_io_num = sda_pin,
        .scl_io_num = scl_pin,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags = {
            .enable_internal_pullup = 1,
        },
    };
    esp_err_t ret = i2c_new_master_bus(&bus_cfg, &g_i2c_bus);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C new master bus failed: %d", ret);
        return ret;
    }

    // --- Hardware reset ---
    // AW9523 RST is active-low; needed on DeepStoa v1 (reference board has PMIC).
    if (rst_pin != GPIO_NUM_NC) {
        gpio_config_t rst_conf = {
            .pin_bit_mask = (1ULL << rst_pin),
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        gpio_config(&rst_conf);
        gpio_set_level(rst_pin, 1);           // drive high
        vTaskDelay(pdMS_TO_TICKS(5));
        gpio_set_level(rst_pin, 0);           // assert reset (low active)
        vTaskDelay(pdMS_TO_TICKS(10));
        gpio_set_level(rst_pin, 1);           // release reset
        vTaskDelay(pdMS_TO_TICKS(10));        // wait for AW9523 boot
        ESP_LOGI(TAG, "HW reset done (GPIO%d)", rst_pin);
    }

    // --- I2C bus scan (all addresses) ---
    ESP_LOGI(TAG, "Scanning I2C bus (port %d, SDA=GPIO%d, SCL=GPIO%d)...",
             i2c_port, sda_pin, scl_pin);
    int devices_found = 0;
    // Scan in reverse so we check higher addresses first (some devices respond slower)
    for (int addr = 1; addr < 127; addr++) {
        esp_err_t probe_ret = i2c_master_probe(g_i2c_bus, addr, pdMS_TO_TICKS(20));
        if (probe_ret == ESP_OK) {
            ESP_LOGI(TAG, "  Device found at 0x%02X (7-bit)", addr);
            devices_found++;
        }
    }
    if (devices_found == 0) {
        ESP_LOGE(TAG, "No I2C devices found on bus! (scanned 1..126)");
        ESP_LOGE(TAG, "Possible causes:");
        ESP_LOGE(TAG, "  1. SDA/SCL pins swapped or wrong GPIO numbers");
        ESP_LOGE(TAG, "  2. Pull-up resistors missing on SDA/SCL");
        ESP_LOGE(TAG, "  3. AW9523 not powered or held in reset");
        ESP_LOGE(TAG, "  4. I2C port conflict with another driver");
        return ESP_ERR_TIMEOUT;
    }

    // --- Add AW9523 device to bus ---
    // Match reference: 400 kHz, no scl_wait_us, no disable_ack_check
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = i2c_addr,
        .scl_speed_hz = 400000,
    };
    ret = i2c_master_bus_add_device(g_i2c_bus, &dev_cfg, &g_i2c_dev);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C add device failed: %d", ret);
        return ret;
    }

    // --- Verify chip ID (matches reference init flow) ---
    uint8_t id;
    ret = aw9523_read_reg(AW9523_REG_ID, &id);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Chip ID read failed (err %d). "
                 "Check I2C wiring, pull-ups, and AW9523 power.", ret);
        return ret;
    }
    if (id != 0x23) {
        ESP_LOGE(TAG, "Unexpected chip ID 0x%02X (expected 0x23)", id);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "AW9523 detected at 0x%02X, chip ID 0x%02X", i2c_addr, id);

    // --- Safe defaults (matching reference: disable interrupts, all inputs) ---
    aw9523_write_reg(AW9523_REG_INTR_P0, 0xFF);  // disable all interrupts P0
    aw9523_write_reg(AW9523_REG_INTR_P1, 0xFF);  // disable all interrupts P1
    aw9523_write_reg(AW9523_REG_DIR_P0, 0xFF);   // all P0 pins input
    aw9523_write_reg(AW9523_REG_DIR_P1, 0xFF);   // all P1 pins input

    // --- Configure for DeepStoa v1 e-paper display ---
    // P0: push-pull mode (bit4=1), not open-drain
    ret = aw9523_write_reg(AW9523_REG_CTL_P0, 0x10);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "CTL_P0 write failed: %d", ret);
        return ret;
    }

    // LED mode: 0 = GPIO mode (not LED current sink)
    aw9523_write_reg(AW9523_REG_LEDMODE_P0, 0x00);
    aw9523_write_reg(AW9523_REG_LEDMODE_P1, 0x00);

    // P0 pin directions: bit5=INT(input), others output
    g_dir_cache[0] = (1 << 5);
    g_out_cache[0] = 0x00;
    aw9523_write_reg(AW9523_REG_DIR_P0, g_dir_cache[0]);
    aw9523_write_reg(AW9523_REG_OUTPUT_P0, g_out_cache[0]);

    // P1 pin directions: bit6=BUSY(input), others output. CS(P1.4)=high (inactive)
    g_dir_cache[1] = (1 << 6);
    g_out_cache[1] = (1 << 4);
    aw9523_write_reg(AW9523_REG_DIR_P1, g_dir_cache[1]);
    aw9523_write_reg(AW9523_REG_OUTPUT_P1, g_out_cache[1]);

    ESP_LOGI(TAG, "Initialized at addr 0x%02X (I2C port %d, 400 kHz)",
             i2c_addr, i2c_port);
    return ESP_OK;
}

void aw9523_reset(void)
{
    aw9523_write_reg(AW9523_REG_RESET, 0x00);
}

uint8_t aw9523_get_chip_id(void)
{
    uint8_t id = 0;
    aw9523_read_reg(AW9523_REG_ID, &id);
    return id;
}

void aw9523_set_port_dir(uint8_t port, uint8_t dir)
{
    if (port > 1) return;
    g_dir_cache[port] = dir;
    aw9523_write_reg(AW9523_REG_DIR_P0 + port, dir);
}

void aw9523_write_port(uint8_t port, uint8_t value)
{
    if (port > 1) return;
    g_out_cache[port] = value;
    aw9523_write_reg(AW9523_REG_OUTPUT_P0 + port, value);
}

uint8_t aw9523_read_port(uint8_t port)
{
    if (port > 1) return 0;
    uint8_t data = 0;
    aw9523_read_reg(AW9523_REG_INPUT_P0 + port, &data);
    return data;
}

void aw9523_set_pin(uint8_t pin, uint8_t level)
{
    uint8_t port = AW9523_GET_PORT(pin);
    uint8_t bit = AW9523_GET_BIT(pin);
    if (port > 1 || bit > 7) return;

    if (level) {
        g_out_cache[port] |= (1 << bit);
    } else {
        g_out_cache[port] &= ~(1 << bit);
    }
    aw9523_write_reg(AW9523_REG_OUTPUT_P0 + port, g_out_cache[port]);
}

uint8_t aw9523_get_pin(uint8_t pin)
{
    uint8_t port = AW9523_GET_PORT(pin);
    uint8_t bit = AW9523_GET_BIT(pin);
    if (port > 1 || bit > 7) return 0;

    uint8_t data = 0;
    aw9523_read_reg(AW9523_REG_INPUT_P0 + port, &data);
    return (data >> bit) & 0x01;
}
