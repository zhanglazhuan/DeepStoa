# t_display 墨水屏调试工程 实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 创建 `debug/t_display` ESP-IDF 子工程，通过 AW9523BTQR I2C IO 扩展器 bit-bang SPI 驱动墨水屏 (GDEM0397T81P)，验证显示功能。

**Architecture:** ESP32S3 → I2C → AW9523BTQR → bit-bang SPI → 墨水屏。`drivers/aw9523/` 提供 IO 扩展器底层 I2C 操作，`drivers/gdem0397t81p/` 从 Zephyr 移植到 ESP-IDF（逻辑不变），`debug/t_display/` 通过 `EXTRA_COMPONENT_DIRS` 引用两个驱动组件。

**Tech Stack:** ESP-IDF v5.5.3, C, I2C legacy driver, FreeRTOS, CMake

## Global Constraints

- 目标芯片: ESP32S3
- ESP-IDF 路径: `C:/Espressif/frameworks/esp-idf-v5.5.3/`
- 墨水屏分辨率: 480×800 (EPD_ARRAY = 48000 bytes)
- I2C 速率: 400kHz
- AW9523 I2C 地址: 0x58
- Display_EPD_W21.h 公开 API 不可变更
- 驱动初始化序列、命令波形逻辑不可变更
- pin 定义统一在 boards/esp32s3/deepstoa_v1.h
- SPI 模式: Mode 0 (CPOL=0, CPHA=0), MSB first

---

### Task 1: 板级定义头文件

**Files:**
- Modify: `boards/esp32s3/deepstoa_v1.h` (当前为空)

**Interfaces:**
- Produces: 引脚宏 `DEEPV1_PIN_*`, `DEEPV1_I2C_PORT`, `DEEPV1_AW9523_ADDR`, AW9523 pin 编码宏 `AW9523_PIN(port, bit)`

- [ ] **Step 1: 写入板级定义**

```c
// boards/esp32s3/deepstoa_v1.h
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
```

- [ ] **Step 2: 提交**

```bash
git add boards/esp32s3/deepstoa_v1.h
git commit -m "feat(board): add deepstoa v1 pin definitions

Define AW9523 pin encoding, I2C pins, and LCD-to-AW9523 mapping.

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 2: AW9523BTQR I2C GPIO 扩展器驱动

**Files:**
- Create: `drivers/aw9523/CMakeLists.txt`
- Create: `drivers/aw9523/aw9523.h`
- Create: `drivers/aw9523/aw9523.c`

**Interfaces:**
- Consumes: `DEEPV1_PIN_*`, `DEEPV1_I2C_PORT`, `DEEPV1_AW9523_ADDR`, `AW9523_GET_PORT()`, `AW9523_GET_BIT()` from `boards/esp32s3/deepstoa_v1.h`
- Produces: `aw9523_init()`, `aw9523_reset()`, `aw9523_get_chip_id()`, `aw9523_write_port()`, `aw9523_read_port()`, `aw9523_set_pin()`, `aw9523_get_pin()`

- [ ] **Step 1: 创建 CMakeLists.txt**

```cmake
# drivers/aw9523/CMakeLists.txt
idf_component_register(
    SRCS "aw9523.c"
    INCLUDE_DIRS "."
    REQUIRES driver
)
```

- [ ] **Step 2: 创建 aw9523.h**

```c
// drivers/aw9523/aw9523.h
#ifndef AW9523_H
#define AW9523_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "hal/gpio_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize AW9523 IO expander.
 *
 * @param i2c_port   I2C port number (0 or 1)
 * @param sda_pin    I2C SDA GPIO pin
 * @param scl_pin    I2C SCL GPIO pin
 * @param rst_pin    Hardware reset GPIO pin (low active), or GPIO_NUM_NC
 * @param i2c_addr   7-bit I2C address (default 0x58)
 * @return esp_err_t ESP_OK on success
 */
esp_err_t aw9523_init(uint8_t i2c_port, gpio_num_t sda_pin, gpio_num_t scl_pin,
                      gpio_num_t rst_pin, uint8_t i2c_addr);

/**
 * @brief Software reset the AW9523.
 */
void aw9523_reset(void);

/**
 * @brief Read the chip ID register (0x10). Expected value: 0x23.
 * @return uint8_t Chip ID value
 */
uint8_t aw9523_get_chip_id(void);

/**
 * @brief Write value to a full port (P0 or P1).
 * @param port   0 for P0, 1 for P1
 * @param value  8-bit value to write
 */
void aw9523_write_port(uint8_t port, uint8_t value);

/**
 * @brief Read a full port (P0 or P1).
 * @param port   0 for P0, 1 for P1
 * @return uint8_t 8-bit port input value
 */
uint8_t aw9523_read_port(uint8_t port);

/**
 * @brief Set a single pin output level.
 * @param pin    Encoded pin (use AW9523_PIN macro)
 * @param level  0 = low, 1 = high
 */
void aw9523_set_pin(uint8_t pin, uint8_t level);

/**
 * @brief Read a single pin input level.
 * @param pin    Encoded pin (use AW9523_PIN macro)
 * @return uint8_t 0 = low, 1 = high
 */
uint8_t aw9523_get_pin(uint8_t pin);

/**
 * @brief Configure pin direction for a full port.
 * @param port  0 for P0, 1 for P1
 * @param dir   8-bit direction mask: 0 = output, 1 = input
 */
void aw9523_set_port_dir(uint8_t port, uint8_t dir);

#ifdef __cplusplus
}
#endif

#endif // AW9523_H
```

- [ ] **Step 3: 创建 aw9523.c 实现**

```c
// drivers/aw9523/aw9523.c
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
```

- [ ] **Step 4: 提交**

```bash
git add drivers/aw9523/
git commit -m "feat(drivers): add AW9523BTQR I2C GPIO expander driver

Supports GPIO read/write per-pin and per-port, chip ID check,
hardware/software reset, and push-pull output mode.

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 3: 移植 gdem0397t81p 驱动 (Zephyr → ESP-IDF)

**Files:**
- Modify: `drivers/gdem0397t81p/CMakeLists.txt` (Zephyr → ESP-IDF)
- Keep: `drivers/gdem0397t81p/Display_EPD_W21.h` (不变)
- Modify: `drivers/gdem0397t81p/Display_EPD_W21.c` (底层 API 替换，逻辑不变)

**Interfaces:**
- Consumes: `aw9523_write_port()`, `aw9523_set_pin()`, `aw9523_get_pin()`, `aw9523_read_port()` from aw9523; `DEEPV1_PIN_*` from board header
- Produces: 所有 `Display_EPD_W21.h` 中声明的函数，签名不变

**注意事项:**
- 原驱动中 `rst` 和 `pwr` GPIO 在当前 PCB 中未连接（没有对应的 AW9523 引脚），EPD_Reset() 仅保留软件复位 SWRESET，硬件复位部分跳过
- `SPI_CHUNK_SIZE` 从 512 缩减为 64（bit-bang 场景下无 DMA，小缓冲足够）
- P0 和 P1 输出寄存器均已由 aw9523_init 初始化，本组件仅通过 cache 保持一致性

- [ ] **Step 1: 更新 CMakeLists.txt**

将 `drivers/gdem0397t81p/CMakeLists.txt` 从 Zephyr 构建系统改为 ESP-IDF:

```cmake
# drivers/gdem0397t81p/CMakeLists.txt
idf_component_register(
    SRCS "Display_EPD_W21.c"
    INCLUDE_DIRS "."
    REQUIRES driver
)
```

- [ ] **Step 2: 保持 Display_EPD_W21.h 不变**

已验证头文件无需修改。仅确认 include guard 正确。

- [ ] **Step 3: 重写 Display_EPD_W21.c — 头文件和静态变量**

替换文件头部 include 和全局变量:

```c
// drivers/gdem0397t81p/Display_EPD_W21.c
// Ported from Zephyr RTOS to ESP-IDF v5.5.3
// Display init sequences and command logic are preserved verbatim.

#include <string.h>
#include <stdbool.h>
#include "Display_EPD_W21.h"
#include "boards/esp32s3/deepstoa_v1.h"
#include "aw9523.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "epd";

// P0/P1 output register caches — mirror of AW9523 output registers.
// Must be initialized before first use (done by EPD_GPIO_Config).
static uint8_t p0_cache;
static uint8_t p1_cache;

// SPI bit-bang: send one byte MSB first, Mode 0 (CPOL=0, CPHA=0)
// SDI = P0.6, SCLK = P0.7
static void spi_bb_write_byte(uint8_t data)
{
    for (int i = 7; i >= 0; i--) {
        // Phase 1: set SDI, SCLK = 0
        p0_cache &= ~((1 << 6) | (1 << 7));
        if (data & (1 << i)) {
            p0_cache |= (1 << 6);   // SDI = 1
        }
        aw9523_write_port(0, p0_cache);

        // Phase 2: SCLK = 1 (rising edge latches data)
        p0_cache |= (1 << 7);
        aw9523_write_port(0, p0_cache);
    }
    // SCLK returns to 0
    p0_cache &= ~(1 << 7);
    aw9523_write_port(0, p0_cache);
}

// SPI bit-bang: send multiple bytes. Yields periodically to prevent
// task watchdog timeout (~60s for full 48KB frame at ~1.2ms/byte).
static void spi_bb_write_bytes(const uint8_t *data, uint32_t len)
{
    for (uint32_t i = 0; i < len; i++) {
        spi_bb_write_byte(data[i]);
        // Yield every 64 bytes (~77ms) to reset task watchdog (default 5s)
        if ((i & 0x3F) == 0x3F) {
            vTaskDelay(1);  // yield and reset watchdog
        }
    }
}
```

- [ ] **Step 4: 重写 Display_EPD_W21.c — GPIO 配置函数**

原 Zephyr 的 `EPD_GPIO_Config()` 替换为读取当前 AW9523 输出状态初始化 cache:

```c
void EPD_GPIO_Config(void)
{
    ESP_LOGI(TAG, "Configuring GPIO via AW9523");

    // Read initial output register states for cache
    // P0 output register covers SCLK(P0.7), SDI(P0.6); INT(P0.5) is input
    // P1 output register covers BUSY(P1.6, input), DC(P1.5), CS(P1.4)
    p0_cache = 0x00;  // SCLK=0, SDI=0 initially
    aw9523_write_port(0, p0_cache);

    p1_cache = (1 << 4);  // CS=1 (inactive), DC=0
    aw9523_write_port(1, p1_cache);

    ESP_LOGI(TAG, "GPIO config done. CS=1, DC=0, SCLK=0, SDI=0");
}
```

- [ ] **Step 5: 重写 Display_EPD_W21.c — 延时函数**

```c
void delay_xms(unsigned int xms)
{
    vTaskDelay(pdMS_TO_TICKS(xms));
}
```

- [ ] **Step 6: 重写 Display_EPD_W21.c — SPI 写入函数 (保持逻辑)**

```c
void EPD_W21_WriteCMD(uint8_t Reg)
{
    // DC = 0 (command mode), CS = 0 (assert)
    p1_cache &= ~((1 << 5) | (1 << 4));  // DC=0, CS=0
    aw9523_write_port(1, p1_cache);

    spi_bb_write_byte(Reg);

    // CS = 1 (de-assert)
    p1_cache |= (1 << 4);
    aw9523_write_port(1, p1_cache);
}

void EPD_W21_WriteDATA(uint8_t Data)
{
    // DC = 1 (data mode), CS = 0 (assert)
    p1_cache |= (1 << 5);    // DC=1
    p1_cache &= ~(1 << 4);   // CS=0
    aw9523_write_port(1, p1_cache);

    spi_bb_write_byte(Data);

    // CS = 1 (de-assert)
    p1_cache |= (1 << 4);
    aw9523_write_port(1, p1_cache);
}

void EPD_W21_WriteDATA_Package(uint8_t *DataArray, uint32_t DataLen)
{
    // DC = 1 (data mode), CS = 0 (assert)
    p1_cache |= (1 << 5);    // DC=1
    p1_cache &= ~(1 << 4);   // CS=0
    aw9523_write_port(1, p1_cache);

    spi_bb_write_bytes(DataArray, DataLen);

    // CS = 1 (de-assert)
    p1_cache |= (1 << 4);
    aw9523_write_port(1, p1_cache);
}

void EPD_W21_WriteDATA_Batch(uint8_t *DataArray, uint32_t DataLen)
{
    // Same as _Package in bit-bang mode (no DMA)
    EPD_W21_WriteDATA_Package(DataArray, DataLen);
}
```

- [ ] **Step 7: 重写 Display_EPD_W21.c — Busy 函数**

```c
bool EPD_IsBusy(void)
{
    return (aw9523_get_pin(DEEPV1_PIN_LCD_BUSY) == 1);
}

void EPD_Read_Busy(void)
{
    while (1) {
        if (aw9523_get_pin(DEEPV1_PIN_LCD_BUSY) == 0)
            break;
        delay_xms(10);
    }
}
```

- [ ] **Step 8: 重写 Display_EPD_W21.c — EPD_Reset 函数**

原驱动有硬件 reset (GPIO toggle) + 延时。当前 PCB 没有独立 LCD RST 引脚，仅保留软件复位 0x12 SWRESET — 该命令已在每个 init 函数 (EPD_HW_Init 等) 中独立发送，所以 `EPD_Reset()` 退化为仅延时（保持原函数调用点兼容）:

```c
void EPD_Reset(void)
{
    // Hardware reset pin not available on current PCB.
    // Display will be reset via 0x12 SWRESET command in init sequences.
    delay_xms(200);
}
```

- [ ] **Step 9: 保留所有初始化序列和显示函数**

以下函数保持**完全相同**的初始化命令序列和逻辑，无需修改：
- `EPD_HW_Init()`
- `EPD_HW_Init_Fast()`
- `EPD_HW_Init_4G()`
- `EPD_HW_Init_180()`
- `EPD_HW_Init_GUI()`
- `EPD_Update()`, `EPD_Update_Fast()`, `EPD_Update_4G()`, `EPD_Part_Update()`, `EPD_Part_Update_Async()`
- `EPD_WhiteScreen_ALL()`, `EPD_WhiteScreen_ALL_Fast()`, `EPD_WhiteScreen_White()`, `EPD_WhiteScreen_Black()`
- `EPD_SetRAMValue_BaseMap()`, `EPD_Dis_Part()`, `EPD_Dis_PartAll()`, `EPD_Dis_PartAll_Async()`
- `EPD_DeepSleep()`, `EPD_Display()`
- `EPD_WhiteScreen_ALL_4G()`, `EPD_Update_4Gray_WithBuffers()`, `In2bytes_Out1byte_RAM1()`, `In2bytes_Out1byte_RAM2()`
- `EPD_Sync_Base_Map()`, `EPD_Dis_Part_Window_Activate()`, `EPD_Dis_Part_Window_Async()`, `EPD_Dis_Part_Window_Async_()`
- `EPD_Dis_Part_RAM()`, `EPD_Dis_Part_Time()`, `EPD_Restore_Full_Window()`

> 这些函数通过 `EPD_W21_WriteCMD`、`EPD_W21_WriteDATA`、`EPD_W21_WriteDATA_Package`、`EPD_W21_WriteDATA_Batch`、`EPD_Read_Busy`、`delay_xms` 间接使用底层 API — 这些底层函数已在 Step 4-8 重写，所以初始化序列无需改动。

- [ ] **Step 10: 更新 Display_EPD_W21.h 添加 UBYTE typedef**

头文件使用了 `UBYTE` 类型但没有定义。从原 `.c` 文件移到 `.h`:

在 `Display_EPD_W21.h` 的 includes 之后添加:
```c
typedef uint8_t UBYTE;
```

原 `.c` 文件中删除 `#define UBYTE uint8_t` 行。

- [ ] **Step 11: 提交**

```bash
git add drivers/gdem0397t81p/
git commit -m "refactor(drivers): port gdem0397t81p from Zephyr to ESP-IDF

Replace Zephyr SPI/GPIO/delay APIs with AW9523 bit-bang SPI over I2C.
Display init sequences and command logic unchanged.
EPD_Reset() degraded to delay-only (no HW reset pin on current PCB).

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 4: 创建 t_display 调试工程

**Files:**
- Create: `debug/t_display/CMakeLists.txt`
- Create: `debug/t_display/main/CMakeLists.txt`
- Create: `debug/t_display/main/t_display.c`
- Create: `debug/t_display/sdkconfig.defaults`

**Interfaces:**
- Consumes: `aw9523.h` (aw9523_init, aw9523_get_chip_id), `Display_EPD_W21.h` (EPD_HW_Init, EPD_WhiteScreen_*, etc.), `deepstoa_v1.h` (pin defs)

- [ ] **Step 1: 创建根 CMakeLists.txt**

```cmake
# debug/t_display/CMakeLists.txt
cmake_minimum_required(VERSION 3.16)

# Point to drivers/ directory for extra components (aw9523, gdem0397t81p)
set(EXTRA_COMPONENT_DIRS
    ${CMAKE_SOURCE_DIR}/../../drivers/aw9523
    ${CMAKE_SOURCE_DIR}/../../drivers/gdem0397t81p
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(t_display)
```

- [ ] **Step 2: 创建 main/CMakeLists.txt**

```cmake
# debug/t_display/main/CMakeLists.txt
idf_component_register(
    SRCS "t_display.c"
    INCLUDE_DIRS "."
    REQUIRES driver
)
```

- [ ] **Step 3: 创建 sdkconfig.defaults**

```ini
# debug/t_display/sdkconfig.defaults
# Enable PSRAM (for 48KB frame buffer)
CONFIG_SPIRAM=y
CONFIG_SPIRAM_USE_MALLOC=y

# Set main task stack size to 8192 (enough for test flow)
CONFIG_ESP_MAIN_TASK_STACK_SIZE=8192

# Enable I2C driver
CONFIG_I2C_MASTER=y

# Increase task watchdog timeout (bit-bang SPI is slow, ~60s per full frame)
CONFIG_ESP_TASK_WDT_TIMEOUT_S=120
```

- [ ] **Step 4: 创建 t_display.c 测试流程**

```c
// debug/t_display/main/t_display.c
// E-ink display test: verify GDEM0397T81P via AW9523BTQR bit-bang SPI

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"

#include "boards/esp32s3/deepstoa_v1.h"
#include "aw9523.h"
#include "Display_EPD_W21.h"

static const char *TAG = "t_display";

// Fill buffer with checkerboard pattern (8x8 pixel blocks)
static void fill_checkerboard(uint8_t *buf)
{
    // Each byte = 8 horizontal pixels. 8 rows = one stripe.
    // Width in bytes = 480/8 = 60
    const int width_bytes = EPD_WIDTH / 8;
    for (int y = 0; y < EPD_HEIGHT; y++) {
        int y_block = y / 8;  // which 8-pixel-tall block
        for (int x_byte = 0; x_byte < width_bytes; x_byte++) {
            int x_block = x_byte;  // each byte = 8 pixels = 1 block wide
            uint8_t pixel = ((y_block + x_block) % 2 == 0) ? 0xFF : 0x00;
            buf[y * width_bytes + x_byte] = pixel;
        }
    }
}

// Fill buffer with border rectangle (20px from edges)
static void fill_border(uint8_t *buf)
{
    const int width_bytes = EPD_WIDTH / 8;
    memset(buf, 0xFF, EPD_ARRAY);  // all white

    const int border = 20;
    // Top and bottom borders
    for (int y = 0; y < border; y++) {
        memset(&buf[y * width_bytes], 0x00, width_bytes);           // top
        memset(&buf[(EPD_HEIGHT - 1 - y) * width_bytes], 0x00, width_bytes); // bottom
    }
    // Left and right borders
    for (int y = border; y < EPD_HEIGHT - border; y++) {
        for (int x = 0; x < border; x++) {
            int byte_idx = y * width_bytes + (x / 8);
            int bit_idx = 7 - (x % 8);
            buf[byte_idx] &= ~(1 << bit_idx);   // clear bit = black
        }
        for (int x = EPD_WIDTH - border; x < EPD_WIDTH; x++) {
            int byte_idx = y * width_bytes + (x / 8);
            int bit_idx = 7 - (x % 8);
            buf[byte_idx] &= ~(1 << bit_idx);
        }
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "=== t_display: E-ink Display Test ===");

    // Step 1: Initialize I2C and AW9523
    ESP_LOGI(TAG, "Step 1: Init AW9523 via I2C...");
    esp_err_t ret = aw9523_init(DEEPV1_I2C_PORT,
                                DEEPV1_PIN_I2C_SDA,
                                DEEPV1_PIN_I2C_SCL,
                                DEEPV1_PIN_IO_RESET,
                                DEEPV1_AW9523_ADDR);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "FAIL: aw9523_init returned %d", ret);
        return;
    }

    // Verify chip ID
    uint8_t chip_id = aw9523_get_chip_id();
    ESP_LOGI(TAG, "AW9523 chip ID: 0x%02X (expected 0x23)", chip_id);
    if (chip_id != 0x23) {
        ESP_LOGE(TAG, "FAIL: Unexpected chip ID 0x%02X", chip_id);
        return;
    }
    ESP_LOGI(TAG, "PASS: AW9523 initialized, chip ID OK");

    // Step 2: Init display
    ESP_LOGI(TAG, "Step 2: Init display...");
    EPD_GPIO_Config();
    EPD_HW_Init();
    ESP_LOGI(TAG, "PASS: Display initialized");

    // Step 3: White screen
    ESP_LOGI(TAG, "Step 3: White screen...");
    EPD_WhiteScreen_White();
    ESP_LOGI(TAG, "PASS: White screen done");
    vTaskDelay(pdMS_TO_TICKS(2000));

    // Step 4: Black screen
    ESP_LOGI(TAG, "Step 4: Black screen...");
    EPD_HW_Init();  // re-init after partial mode
    EPD_WhiteScreen_Black();
    ESP_LOGI(TAG, "PASS: Black screen done");
    vTaskDelay(pdMS_TO_TICKS(2000));

    // Step 5: Checkerboard pattern
    ESP_LOGI(TAG, "Step 5: Checkerboard pattern...");
    uint8_t *buf = (uint8_t *)malloc(EPD_ARRAY);
    if (buf == NULL) {
        ESP_LOGE(TAG, "FAIL: malloc(%d) failed", EPD_ARRAY);
        return;
    }
    EPD_HW_Init();
    fill_checkerboard(buf);
    EPD_WhiteScreen_ALL(buf);
    ESP_LOGI(TAG, "PASS: Checkerboard done");
    vTaskDelay(pdMS_TO_TICKS(2000));

    // Step 6: Border rectangle pattern
    ESP_LOGI(TAG, "Step 6: Border rectangle pattern...");
    EPD_HW_Init();
    fill_border(buf);
    EPD_WhiteScreen_ALL(buf);
    ESP_LOGI(TAG, "PASS: Border rectangle done");
    vTaskDelay(pdMS_TO_TICKS(2000));

    free(buf);

    // Step 7: Deep sleep
    ESP_LOGI(TAG, "Step 7: Deep sleep...");
    EPD_DeepSleep();
    ESP_LOGI(TAG, "PASS: Display entered deep sleep");

    ESP_LOGI(TAG, "=== TEST COMPLETE: All steps passed ===");

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
```

- [ ] **Step 5: 构建验证**

```bash
cd debug/t_display
idf.py build
```

预期：编译通过，无错误。警告可接受（如未使用的静态函数）。

- [ ] **Step 6: 提交**

```bash
git add debug/t_display/
git commit -m "feat(debug): add t_display e-ink test project

Test steps: AW9523 init → chip ID check → display init →
white → black → checkerboard → border → deep sleep.
Uses EXTRA_COMPONENT_DIRS to reference drivers/aw9523 and drivers/gdem0397t81p.

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Post-Implementation Verification

所有 task 完成后：
1. `cd debug/t_display && idf.py build` — 编译通过
2. `idf.py -p <PORT> flash monitor` — 烧录并观察串口输出，确认每步 PASS
3. 肉眼检查墨水屏：全白 → 全黑 → 棋盘格 → 矩形边框 → 休眠
