#include "ft6336u_touch.h"
#include "ft6336u_report.h"
#include <esp_log.h>
#include <cstring>

static const char *TAG = "FT6336U";

static esp_err_t read_reg(ft6336u_handle_t *h, uint8_t reg, uint8_t *data, size_t len) {
    return i2c_master_transmit_receive(h->dev_handle, &reg, 1, data, len, 30);
}

void ft6336u_deinit(ft6336u_handle_t *h) {
    if (!h) return;
    if (h->dev_handle) i2c_master_bus_rm_device(h->dev_handle);
    *h = {};
}

bool ft6336u_init(i2c_master_bus_handle_t bus, uint8_t addr, ft6336u_handle_t *h) {
    if (!bus || !h || h->dev_handle) return false;
    if (i2c_master_probe(bus, addr, 50) != ESP_OK) {
        ESP_LOGW(TAG, "No touch controller at 0x%02x", addr);
        return false;
    }
    i2c_device_config_t cfg = {};
    cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    cfg.device_address = addr;
    cfg.scl_speed_hz = 100000;
    esp_err_t ret = i2c_master_bus_add_device(bus, &cfg, &h->dev_handle);
    if (ret != ESP_OK) return false;

    // A4 is interrupt/polling mode, not chip ID. Preserve factory sensitivity
    // and firmware. Do not report success after failed reads/configuration.
    uint8_t id = 0, version = 0, mode = 0;
    ret = read_reg(h, 0xA3, &id, 1);
    if (ret == ESP_OK) ret = read_reg(h, 0xA6, &version, 1);
    if (ret == ESP_OK) ret = read_reg(h, 0x00, &mode, 1);
    if (ret == ESP_OK && (mode & 0x70) != 0) {
        const uint8_t working[] = {0x00, 0x00};
        ret = i2c_master_transmit(h->dev_handle, working, sizeof(working), 30);
    }
    if (ret == ESP_OK) {
        const uint8_t polling[] = {0xA4, 0x00};
        ret = i2c_master_transmit(h->dev_handle, polling, sizeof(polling), 30);
    }
    if (ret != ESP_OK || !ft6336u_read(h)) {
        ESP_LOGW(TAG, "Touch setup/report failed at 0x%02x (setup=%s)", addr, esp_err_to_name(ret));
        ft6336u_deinit(h);
        return false;
    }
    ESP_LOGI(TAG, "Touch ready address=0x%02x chip_id(A3)=0x%02x firmware(A6)=0x%02x", addr, id, version);
    return true;
}

bool ft6336u_read(ft6336u_handle_t *h) {
    if (!h || !h->dev_handle) return false;
    h->num_points = 0;
    std::memset(h->points, 0, sizeof(h->points));
    uint8_t report[13];
    if (read_reg(h, 0x02, report, sizeof(report)) != ESP_OK) return false;
    Ft6336Point points[2];
    uint8_t count = 0;
    if (!DecodeFt6336Report(report, sizeof(report), points, count)) return false;
    for (unsigned i = 0; i < count; ++i) {
        h->points[i] = {points[i].x, points[i].y, points[i].pressure, points[i].id, true};
    }
    h->num_points = count;
    return true;
}

uint8_t ft6336u_get_num_points(const ft6336u_handle_t *h) { return h ? h->num_points : 0; }
const ft6336u_touch_point_t *ft6336u_get_point(const ft6336u_handle_t *h, int index) {
    return h && index >= 0 && index < h->num_points ? &h->points[index] : nullptr;
}
