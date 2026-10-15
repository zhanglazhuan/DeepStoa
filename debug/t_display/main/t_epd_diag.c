// debug/t_display/main/t_epd_diag.c
// 临时诊断程序：屏"点不亮"时，把范围收窄到 POWER / RESET / DC / BUSY 四根线。
//
// 前提：CS / SCLK / SDI 的波形已经用逻辑分析仪确认正常，所以这里完全不看位
// 时序，只查四件事：
//   ① AW9523 的 P1.5/P1.6/P1.7 能不能真的被驱动到高电平。若这几个脚还停在
//      LED 恒流模式（寄存器 0x12/0x13 的位为 0），就只能灌电流、拉不高，
//      RESET 出不了高电平（屏被永久按在复位态）、POWER 也上不了电。
//   ② POWER 拉高之后，LCD_15V / LCD_3V3 到底有没有起来。
//   ③ BUSY 在复位前后、以及收到刷新命令后会不会动。
//   ④ 完整走一遍驱动流程能不能点亮。
//
// 每一步都打印"回读值"（读 AW9523 输入寄存器）并停留足够久，方便同时用
// 万用表/示波器量 AW9523 的引脚。**回读和实测不一致 = 该脚驱动不了。**
//
// 编译开关：
//   -DEPD_SPI_CLOCK_HZ=<hz>   SPI 时钟（默认 10 MHz）
//   -DDIAG_DWELL_MS=<ms>      每个观察点停留多久（默认 15000）

#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/gpio.h"

#include "epd_display.h"
#include "deepstoa_v1.h"
#include "aw9523.h"

static const char *TAG = "epd_diag";

#ifndef DIAG_DWELL_MS
#define DIAG_DWELL_MS 15000
#endif

// ─── Helpers ───────────────────────────────────────────────────────────

static unsigned aw_dc(void)    { return aw9523_get_pin(DEEPV1_AW_PIN_EPD_DC); }
static unsigned aw_reset(void) { return aw9523_get_pin(DEEPV1_AW_PIN_EPD_RESET); }
static unsigned aw_power(void) { return aw9523_get_pin(DEEPV1_AW_PIN_EPD_POWER); }
static int      busy(void)     { return gpio_get_level(DEEPV1_PIN_EPD_BUSY); }

// 一行打完四根线的状态：DC / RESET / POWER 是 AW9523 回读，BUSY 是直连 GPIO。
static void log_ctrl(const char *what)
{
    ESP_LOGI(TAG, "%-26s | DC(P1.5)=%u RESET(P1.6)=%u POWER(P1.7)=%u | BUSY(GPIO%d)=%d",
             what, aw_dc(), aw_reset(), aw_power(), DEEPV1_PIN_EPD_BUSY, busy());
}

// 盯着 BUSY 采样 ms 毫秒，每 100 ms 打一行。
static void watch_busy(const char *what, int ms)
{
    ESP_LOGI(TAG, "watch BUSY: %s", what);
    for (int t = 0; t <= ms; t += 100) {
        ESP_LOGI(TAG, "  BUSY t=%5d ms = %d", t, busy());
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

// ─── A: 引脚驱动能力 ───────────────────────────────────────────────────

// 每个脚单独拉高、拉低，回读输入寄存器。
// 输入寄存器反映的是引脚上的实际电平，所以：
//   写 1 回读 1 / 写 0 回读 0  → 推挽驱动正常
//   写 1 回读 0               → 该脚驱动不了（LED 恒流模式，或外部被强钳位）
static void step_a_pin_drive(void)
{
    static const struct {
        const char *name;
        uint8_t     pin;
    } lines[] = {
        { "DC    (P1.5)", DEEPV1_AW_PIN_EPD_DC    },
        { "RESET (P1.6)", DEEPV1_AW_PIN_EPD_RESET },
        { "POWER (P1.7)", DEEPV1_AW_PIN_EPD_POWER },
    };

    ESP_LOGI(TAG, "──────── A: 引脚驱动能力（万用表量，每档停 %d ms）────────",
             DIAG_DWELL_MS);

    for (size_t i = 0; i < sizeof(lines) / sizeof(lines[0]); i++) {
        // 高电平档
        aw9523_set_pin(lines[i].pin, 1);
        ESP_LOGI(TAG, ">>> %s = 1，表笔量这个脚，应该读到高 <<<", lines[i].name);
        vTaskDelay(pdMS_TO_TICKS(DIAG_DWELL_MS));
        unsigned hi = aw9523_get_pin(lines[i].pin);
        ESP_LOGI(TAG, "    回读 = %u", hi);

        // 低电平档
        aw9523_set_pin(lines[i].pin, 0);
        ESP_LOGI(TAG, ">>> %s = 0，表笔量这个脚，应该读到低 <<<", lines[i].name);
        vTaskDelay(pdMS_TO_TICKS(DIAG_DWELL_MS));
        unsigned lo = aw9523_get_pin(lines[i].pin);
        ESP_LOGI(TAG, "    回读 = %u", lo);

        bool ok = (hi == 1 && lo == 0);
        ESP_LOGI(TAG, "%s  写1→回读%u  写0→回读%u   %s",
                 lines[i].name, hi, lo,
                 ok ? "OK"
                    : "!! 回读不跟随 — 这个脚驱动不了（查 LEDMODE / 外部钳位）");
    }

    // 收尾：三根线全部置低。此时 RESET=0 表示屏处于复位态，是后面几步的起点。
    aw9523_set_pin(DEEPV1_AW_PIN_EPD_DC, 0);
    aw9523_set_pin(DEEPV1_AW_PIN_EPD_RESET, 0);
    aw9523_set_pin(DEEPV1_AW_PIN_EPD_POWER, 0);
    log_ctrl("A 结束（全部拉低）");
}

// ─── B: 上电，观察 LCD_15V ─────────────────────────────────────────────

static void step_b_power_on(void)
{
    ESP_LOGI(TAG, "──────── B: POWER 上电 + LCD_15V 观察 ────────");

    aw9523_set_pin(DEEPV1_AW_PIN_EPD_RESET, 0);   // 先保持复位
    aw9523_set_pin(DEEPV1_AW_PIN_EPD_POWER, 1);   // 再上电
    ESP_LOGI(TAG, "POWER=1，先等 500 ms 让 Q4/Q3 和 U6 升压起来...");
    vTaskDelay(pdMS_TO_TICKS(500));
    log_ctrl("POWER=1 (500ms 后)");

    ESP_LOGI(TAG, ">>> 现在用万用表量 LCD_15V 和 LCD_3V3，停留 %d ms <<<", DIAG_DWELL_MS);
    vTaskDelay(pdMS_TO_TICKS(DIAG_DWELL_MS));
}

// ─── C: 复位脉冲，观察 BUSY ────────────────────────────────────────────

static void step_c_reset_busy(void)
{
    ESP_LOGI(TAG, "──────── C: 复位脉冲 + BUSY 观察 ────────");

    log_ctrl("复位前");
    aw9523_set_pin(DEEPV1_AW_PIN_EPD_RESET, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    log_ctrl("RESET=0 拉低 20ms");

    aw9523_set_pin(DEEPV1_AW_PIN_EPD_RESET, 1);
    vTaskDelay(pdMS_TO_TICKS(20));
    log_ctrl("RESET=1 释放 20ms");

    // 屏活着的话，复位释放后 BUSY 应该先忙一阵再回落到 0。
    // 如果这 2 秒里 BUSY 一直是同一个值不动 → 屏根本没在响应复位。
    watch_busy("复位释放后", 2000);
}

// ─── D: 刷新命令后 BUSY 有没有反应 ─────────────────────────────────────

static void step_d_busy_response(uint8_t *buf)
{
    ESP_LOGI(TAG, "──────── D: 发一次异步刷新，看 BUSY 反应 ────────");

    // 全黑，刷新前后最容易在屏上看出来。
    memset(buf, 0x00, EPD_ARRAY);

    log_ctrl("发命令前");
    // 这个 API 发完整帧 + 0x22/0x20 但**不等 BUSY**，正好用来观察 BUSY 的跳变。
    EPD_Dis_PartAll_Async(buf);
    log_ctrl("命令已发完");

    watch_busy("0x20 触发后", 4000);
}

// ─── E: 完整驱动一遍 ───────────────────────────────────────────────────

static void step_e_full_drive(uint8_t *buf)
{
    ESP_LOGI(TAG, "──────── E: 完整驱动一遍 ────────");

    aw9523_set_pin(DEEPV1_AW_PIN_EPD_POWER, 1);
    ESP_LOGI(TAG, "POWER=1，等 500 ms 再复位（给电源充分爬升时间）");
    vTaskDelay(pdMS_TO_TICKS(500));

    memset(buf, 0x00, EPD_ARRAY);   // 全黑
    EPD_HW_Init();                  // 复位 + 完整 init 序列
    EPD_WhiteScreen_ALL(buf);       // 写帧 + update（带 BUSY 超时保护）

    ESP_LOGI(TAG, ">>> 看屏有没有变黑，停留 10 秒 <<<");
    vTaskDelay(pdMS_TO_TICKS(10000));
}

// ─── Main ──────────────────────────────────────────────────────────────

void app_main(void)
{
    ESP_LOGI(TAG, "=== t_epd_diag: POWER / RESET / DC / BUSY 诊断 ===");
    ESP_LOGI(TAG, "SPI2 直连: CS=%d SCLK=%d SDI=%d BUSY=%d; DC/RESET/POWER 在 AW9523 P1.5/1.6/1.7",
             DEEPV1_PIN_EPD_CS, DEEPV1_PIN_EPD_SCK, DEEPV1_PIN_EPD_MOSI,
             DEEPV1_PIN_EPD_BUSY);

    epd_gpio_config();   // AW9523(I2C) + CS/BUSY GPIO + SPI2，与正式固件同路径
    ESP_LOGI(TAG, "AW9523 chip ID = 0x%02X (期望 0x23)", aw9523_get_chip_id());
    log_ctrl("epd_gpio_config 之后");

    uint8_t *buf = (uint8_t *)malloc(EPD_ARRAY);
    if (buf == NULL) {
        ESP_LOGE(TAG, "FAIL: malloc(%d) failed", EPD_ARRAY);
        return;
    }

    step_a_pin_drive();
    step_b_power_on();
    step_c_reset_busy();
    step_d_busy_response(buf);
    step_e_full_drive(buf);

    ESP_LOGI(TAG, "=== 诊断流程结束，之后每秒打印一次状态 ===");
    for (;;) {
        log_ctrl("idle");
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
