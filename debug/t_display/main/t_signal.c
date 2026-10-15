// debug/t_display/main/t_signal.c
// 临时程序：完整驱动 GDEM0397T81P，供逻辑分析仪抓取 CS / SCLK / SDI 波形。
//
// 和 t_display.c 的区别只是"怎么用"而不是"用什么"：这里不画图、不等人工测量，
// 只反复跑一遍完整的正常驱动流程，每轮之间留固定空档并打印轮次号，方便在
// 分析仪上设触发、按日志对齐时间轴。
//
// 底层复用 epd_display.c 的真实驱动路径 —— AW9523（I2C0）负责 POWER / RESET /
// DC 和 BUSY 回读，SPI2 走 CS / SCLK / SDI —— 所以抓到的波形和正式固件一致，
// 不是临时拼出来的仿制品。
//
// 编译开关：
//   -DEPD_SPI_CLOCK_HZ=<hz>    改 SPI 时钟，抓波形时可以降到 1 MHz（默认 10 MHz）
//   -DEPD_BUSY_TIMEOUT_MS=<ms> BUSY 等待上限（默认 10000）
//   -DSIGNAL_ROUND_GAP_MS=<ms> 每轮之间的空档（默认 2000）

#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "epd_display.h"
#include "deepstoa_v1.h"
#include "aw9523.h"

static const char *TAG = "t_signal";

// 每轮之间的静默空档。分析仪可以在空档里重新武装触发，也便于用日志时间戳
// 把抓到的波形和轮次号对上。
#ifndef SIGNAL_ROUND_GAP_MS
#define SIGNAL_ROUND_GAP_MS 2000
#endif

// ─── Frame patterns ────────────────────────────────────────────────────

// 按轮次循环三种图案，每一种在 SDI 上都是一眼能认出来的形状：
// 常量字节用来确认电平和位时序，交替位用来确认位序和边沿。
//
// RAM 按列寻址：一列 EPD_HEIGHT/8 字节，每字节含 8 个纵向像素。这里三种图案
// 都是整个缓冲区一致的，不需要按列处理。
static const char *fill_frame(uint8_t *buf, unsigned round)
{
    switch (round % 3) {
    case 0:
        memset(buf, 0xFF, EPD_ARRAY);
        return "0xFF: SDI all bits high";
    case 1:
        memset(buf, 0x00, EPD_ARRAY);
        return "0x00: SDI all bits low";
    default:
        for (size_t i = 0; i < EPD_ARRAY; i++) {
            buf[i] = (i & 1) ? 0x55 : 0xAA;
        }
        return "0xAA/0x55: SDI alternating bits";
    }
}

// ─── Main sequence ─────────────────────────────────────────────────────

void app_main(void)
{
    ESP_LOGI(TAG, "=== t_signal: full display bring-up for logic-analyser capture ===");
    ESP_LOGI(TAG, "SPI2 direct: CS=%d SCLK=%d SDI=%d BUSY=%d; DC/RESET/POWER on AW9523 (I2C0)",
             DEEPV1_PIN_EPD_CS, DEEPV1_PIN_EPD_SCK, DEEPV1_PIN_EPD_MOSI,
             DEEPV1_PIN_EPD_BUSY);

    // AW9523（I2C0：SDA=17 / SCL=18）+ CS/BUSY GPIO + SPI2 bus，
    // 和正式固件走的是同一条路径。
    epd_gpio_config();

    uint8_t *buf = (uint8_t *)malloc(EPD_ARRAY);
    if (buf == NULL) {
        ESP_LOGE(TAG, "FAIL: malloc(%d) failed", EPD_ARRAY);
        return;
    }

    for (unsigned round = 1;; round++) {
        // 复位 + 完整 init 命令序列。这一段在 SCLK/SDI 上是逐条短命令、
        // 每条一个 CS 脉冲，最容易逐条核对。
        ESP_LOGI(TAG, "--- round %u: EPD_HW_Init ---", round);
        EPD_HW_Init();

        // 一帧 96 KB 数据 + update 触发。这一段是单个长 CS 低电平里连续
        // 输出 0x24 的数据流。
        const char *pattern = fill_frame(buf, round);
        ESP_LOGI(TAG, "--- round %u: frame write, %s ---", round, pattern);
        EPD_WhiteScreen_ALL(buf);

        ESP_LOGI(TAG, "--- round %u done; %d ms gap ---", round, SIGNAL_ROUND_GAP_MS);
        vTaskDelay(pdMS_TO_TICKS(SIGNAL_ROUND_GAP_MS));
    }
}
