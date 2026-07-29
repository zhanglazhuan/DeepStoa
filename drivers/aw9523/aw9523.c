// drivers/aw9523/aw9523.c
// AW9523BTQR I2C GPIO Expander Driver Implementation

#include "aw9523.h"
#include "boards/esp32s3/deepstoa_v1.h"
#include "driver/i2c.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "aw9523";

// Register map
#define AW9523_REG_INPUT_P0     0x00
#define AW9523_REG_INPUT_P1     0x01
#define AW9523_REG_OUTPUT_P0    0x02
#define AW9523_REG_OUTPUT_P1    0x03
#define AW9523_REG_DIR_P0       0x04
#define AW9523_REG_DIR_P1       0x05
#define AW9523_REG_ID           0x10
#define AW9523_REG_CTL_P0       0x11
#define AW9523_REG_RESET        0x7F

static uint8_t g_i2c_addr;
static uint8_t g_i2c_port;
static uint8_t g_out_cache[2];  // P0, P1 output register cache
static uint8_t g_dir_cache[2];  // P0, P1 direction register cache

static esp_err_t aw9523_read_reg(uint8_t reg, uint8_t *data)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (g_i2c_addr << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (g_i2c_addr << 1) | I2C_MASTER_READ, true);
    i2c_master_read_byte(cmd, data, I2C_MASTER_NACK);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(g_i2c_port, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    return ret;
}

static esp_err_t aw9523_write_reg(uint8_t reg, uint8_t data)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (g_i2c_addr << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);
    i2c_master_write_byte(cmd, data, true);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(g_i2c_port, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    return ret;
}

esp_err_t aw9523_init(uint8_t i2c_port, gpio_num_t sda_pin, gpio_num_t scl_pin,
                      gpio_num_t rst_pin, uint8_t i2c_addr)
{
    g_i2c_addr = i2c_addr;
    g_i2c_port = i2c_port;

    // Configure I2C
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = sda_pin,
        .scl_io_num = scl_pin,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
    };
    conf.master.clk_speed = 400000;

    esp_err_t ret = i2c_param_config(i2c_port, &conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C param config failed: %d", ret);
        return ret;
    }

    ret = i2c_driver_install(i2c_port, conf.mode, 0, 0, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C driver install failed: %d", ret);
        return ret;
    }

    // Hardware reset
    if (rst_pin != GPIO_NUM_NC) {
        gpio_config_t rst_conf = {
            .pin_bit_mask = (1ULL << rst_pin),
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        gpio_config(&rst_conf);
        gpio_set_level(rst_pin, 1);
        vTaskDelay(pdMS_TO_TICKS(1));
        gpio_set_level(rst_pin, 0);   // assert reset (low active)
        vTaskDelay(pdMS_TO_TICKS(1));
        gpio_set_level(rst_pin, 1);   // release reset
        vTaskDelay(pdMS_TO_TICKS(5));
    }

    // Software reset
    aw9523_write_reg(AW9523_REG_RESET, 0x00);
    vTaskDelay(pdMS_TO_TICKS(5));

    // Set P0 to push-pull mode (not LED mode)
    aw9523_write_reg(AW9523_REG_CTL_P0, 0x10);

    // Default direction: set based on pin usage
    // P0: bit7=SCLK(out) bit6=SDI(out) bit5=INT(in) bits4-0=unused(out)
    g_dir_cache[0] = (1 << 5);  // only INT is input
    g_out_cache[0] = 0x00;
    aw9523_write_reg(AW9523_REG_DIR_P0, g_dir_cache[0]);
    aw9523_write_reg(AW9523_REG_OUTPUT_P0, g_out_cache[0]);

    // P1: bit6=BUSY(in) bit5=DC(out) bit4=CS(out) others unused(out)
    g_dir_cache[1] = (1 << 6);  // only BUSY is input
    g_out_cache[1] = (1 << 4);  // CS high (inactive)
    aw9523_write_reg(AW9523_REG_DIR_P1, g_dir_cache[1]);
    aw9523_write_reg(AW9523_REG_OUTPUT_P1, g_out_cache[1]);

    ESP_LOGI(TAG, "Initialized at addr 0x%02X", g_i2c_addr);
    return ESP_OK;
}

void aw9523_reset(void)
{
    aw9523_write_reg(AW9523_REG_RESET, 0x00);
}

uint8_t aw9523_get_chip_id(void)
{
    uint8_t id = 0;
    esp_err_t ret = aw9523_read_reg(AW9523_REG_ID, &id);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Read chip ID failed: %d", ret);
    }
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
