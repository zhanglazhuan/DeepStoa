// debug/t_display/main/t_busy_diag.c
// 临时诊断：查 BUSY 信号到底是谁在驱动、什么时候会变。
//
// 背景：DC / POWER / RESET 的实测电压正常，SPI 波形也正常，只有屏没反应。
// BUSY 是唯一还没被证实正常的信号（直连 GPIO21，不经过 AW9523）。
//
// BUSY 有两种坏法，都会让屏看起来"完全没反应"：
//   恒高 → epd_read_busy() 每次都等到超时，命令发得太晚；
//   恒低 → 驱动以为刷新瞬间完成，在波形跑完之前就把屏复位了。
//
// 所以分五步查（⓪ 只查引脚本身，跟屏无关）：
//   ⓪ 把 GPIO21 当推挽输出驱动。内部上下拉太弱压不动负载，推挽能。推挽都
//      到不了轨，就说明这条网被外力硬钳住了（板子/焊点问题，与屏无关）。
//   ① 用内部上拉/下拉当"探针"，**断电和上电各测一遍**。断电时读到 0.8 V
//      左右是正常的（屏的 ESD 二极管钳在死电源轨上），只能证明线是通的。
//      要看的是上电后三种配置读数有没有差别：一样 = 屏在驱动（活的），
//      跟着上下拉满量程变 = 没人驱动，上电后还在 0.4~0.8 V 徘徊 = 屏的
//      电源轨根本没起来。
//   ② 上电 + 复位后连续采样，看 BUSY 会不会动。
//   ③ 触发一次刷新，看 BUSY 在波形期间有没有拉高。
//   ④ 换成带上拉的配置完整驱动一遍，看屏会不会亮。
//
// 编译开关：
//   -DEPD_SPI_CLOCK_HZ=<hz>   SPI 时钟（默认 10 MHz）
//   -DPROBE_DWELL_MS=<ms>     每个上下拉档停留多久（默认 10000，要够切万用表档位）

#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/gpio.h"

#include "epd_display.h"
#include "deepstoa_v1.h"
#include "aw9523.h"

static const char *TAG = "busy_diag";

static int busy(void) { return gpio_get_level(DEEPV1_PIN_EPD_BUSY); }

// ─── BUSY 波形条 ───────────────────────────────────────────────────────

// 连续采样 3 秒（每 10 ms 一次），画成 60 格的波形条：每格 50 ms。
//   '#' = 这一格全程为高    '.' = 全程为低    'x' = 格内有跳变
// 同时统计高电平占比和跳变次数 —— 数字比图形更能说明问题。
#define BUSY_TRACE_MS    3000
#define BUSY_TICK_MS     10
#define BUSY_BUCKETS     60
#define BUSY_PER_BUCKET  (BUSY_TRACE_MS / BUSY_BUCKETS / BUSY_TICK_MS)

static void trace_busy(const char *what)
{
    char bar[BUSY_BUCKETS + 1];
    int  transitions = 0, prev = -1;
    long total_high = 0, total = 0;

    for (int b = 0; b < BUSY_BUCKETS; b++) {
        int highs = 0;
        for (int s = 0; s < BUSY_PER_BUCKET; s++) {
            int lv = busy();
            highs    += lv;
            total_high += lv;
            total++;
            if (prev >= 0 && lv != prev) transitions++;
            prev = lv;
            vTaskDelay(pdMS_TO_TICKS(BUSY_TICK_MS));
        }
        bar[b] = (highs == BUSY_PER_BUCKET) ? '#'
               : (highs == 0)              ? '.' : 'x';
    }
    bar[BUSY_BUCKETS] = '\0';

    ESP_LOGI(TAG, "BUSY [%s]", what);
    ESP_LOGI(TAG, "  |%s|", bar);
    ESP_LOGI(TAG, "  高电平占比 %ld%%，跳变 %d 次  (#=一直高  .=一直低  x=格内抖动)",
             total ? total_high * 100 / total : 0, transitions);
}

// ─── ① 上下拉探针 ──────────────────────────────────────────────────────

// 每档停留多久。要手动切万用表档位，太短了跟不上。
#ifndef PROBE_DWELL_MS
#define PROBE_DWELL_MS 10000
#endif

static void probe_pull(const char *what, gpio_pull_mode_t mode)
{
    const int tick_ms = 100;
    const int samples = PROBE_DWELL_MS / tick_ms;

    gpio_set_pull_mode(DEEPV1_PIN_EPD_BUSY, mode);
    vTaskDelay(pdMS_TO_TICKS(100));   // 等电平稳定

    ESP_LOGI(TAG, ">>> %s：现在量 BUSY(GPIO%d)，停留 %d ms <<<",
             what, DEEPV1_PIN_EPD_BUSY, PROBE_DWELL_MS);

    // 停留期间一直采样：既方便万用表读数，也顺带记下这一段的高低占比。
    int highs = 0;
    for (int i = 0; i < samples; i++) {
        highs += busy();
        vTaskDelay(pdMS_TO_TICKS(tick_ms));
    }
    ESP_LOGI(TAG, "  %s → BUSY 高电平 %d/%d", what, highs, samples);
}

// ─── ⓪ 推挽驱动能力 ────────────────────────────────────────────────────

// 内部上下拉只有 ~45 kΩ，太弱，压不动任何真实负载。这里干脆把 GPIO21 当
// 推挽输出用（驱动电流 ~40 mA，比上拉强三个数量级），看它能不能到达轨。
//
// GPIO_MODE_INPUT_OUTPUT 下 gpio_get_level() 读的是**焊盘上的实际电平**，
// 所以不用万用表也能判断：
//   高回读 1、低回读 0        → 引脚和走线是好的，之前那些怪读数只是弱漏电
//   高回读到不了 1（卡在 0.8） → 有东西硬钳位，板子/焊点问题
static void step_output_drive(void)
{
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << DEEPV1_PIN_EPD_BUSY,
        .mode         = GPIO_MODE_INPUT_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);

    ESP_LOGI(TAG, "──────── ⓪ 推挽驱动能力（万用表量 GPIO%d）────────",
             DEEPV1_PIN_EPD_BUSY);

    for (int level = 1; level >= 0; level--) {
        gpio_set_level(DEEPV1_PIN_EPD_BUSY, level);
        ESP_LOGI(TAG, ">>> 推挽输出 %d，量 GPIO%d，停留 %d ms <<<",
                 level, DEEPV1_PIN_EPD_BUSY, PROBE_DWELL_MS);
        vTaskDelay(pdMS_TO_TICKS(PROBE_DWELL_MS));
        ESP_LOGI(TAG, "    回读 = %d   %s", gpio_get_level(DEEPV1_PIN_EPD_BUSY),
                 gpio_get_level(DEEPV1_PIN_EPD_BUSY) == level
                     ? "OK — 能驱动到轨"
                     : "!! 到不了轨 — 这条网被外力钳住了");
    }

    // 测完恢复成输入，后面的探针步骤要用。
    gpio_set_direction(DEEPV1_PIN_EPD_BUSY, GPIO_MODE_INPUT);
    gpio_set_pull_mode(DEEPV1_PIN_EPD_BUSY, GPIO_FLOATING);
}

// ─── Main ──────────────────────────────────────────────────────────────

void app_main(void)
{
    ESP_LOGI(TAG, "=== t_busy_diag: BUSY 是谁在驱动 ===");

    epd_gpio_config();   // AW9523(I2C) + CS/BUSY GPIO + SPI2，与正式固件同路径
    ESP_LOGI(TAG, "AW9523 chip ID = 0x%02X", aw9523_get_chip_id());

    // 先把引脚本身查清楚，再谈屏。这一步跟屏在不在、上没上电都无关。
    step_output_drive();
    ESP_LOGI(TAG, ">>> ⓪ 结束，现在请拔掉屏排线，准备 ①a <<<");
    vTaskDelay(pdMS_TO_TICKS(5000));

    uint8_t *buf = (uint8_t *)malloc(EPD_ARRAY);
    if (buf == NULL) {
        ESP_LOGE(TAG, "FAIL: malloc(%d) failed", EPD_ARRAY);
        return;
    }

    // ── ① 上下拉探针：断电 / 上电各测一遍 ────────────────────────────
    //
    // 必须测两次。屏没上电时，它 BUSY 脚上的 ESD 二极管等效于接在死电源轨
    // 上的一个 PN 结：内部上拉往 3.3 V 灌，会在约一个压降（0.7~0.9 V）处被
    // 钳住。所以"断电时读到 0.8 V 左右"是**正常**的，只能说明线是通的，不
    // 能说明屏是坏的。
    //
    // 真正要看的是**上电之后**三种配置读数有没有差别：
    //   三种读数完全一样（都 ≈0 或都 ≈3.3） → 屏在驱动它，屏是活的 ✅
    //   读数跟着上下拉满量程变（3.3 / 0 不等） → 线上没人驱动 ❌
    //   上电后仍在 0.4~0.8 V 徘徊           → 屏的电源轨还是死的 → 查供电 ❌
    ESP_LOGI(TAG, "──── ① 内部上下拉探针（断电 / 上电各一遍）────");

    ESP_LOGI(TAG, "--- ①a: POWER=0，屏没上电（当基线）---");
    probe_pull("浮空", GPIO_FLOATING);
    probe_pull("上拉", GPIO_PULLUP_ONLY);
    probe_pull("下拉", GPIO_PULLDOWN_ONLY);

    // 上电：先按住复位，再给电，然后等电源爬升稳下来。
    aw9523_set_pin(DEEPV1_AW_PIN_EPD_RESET, 0);
    aw9523_set_pin(DEEPV1_AW_PIN_EPD_POWER, 1);
    ESP_LOGI(TAG, "POWER=1，等 1000 ms 让电源稳下来...");
    vTaskDelay(pdMS_TO_TICKS(1000));

    ESP_LOGI(TAG, "--- ①b: POWER=1，屏已上电（RESET 仍按住）---");
    probe_pull("浮空", GPIO_FLOATING);
    probe_pull("上拉", GPIO_PULLUP_ONLY);
    probe_pull("下拉", GPIO_PULLDOWN_ONLY);

    // ── ② 复位释放，看 BUSY 动不动 ───────────────────────────────────
    ESP_LOGI(TAG, "──── ② 上电 + 复位 ────");
    gpio_set_pull_mode(DEEPV1_PIN_EPD_BUSY, GPIO_FLOATING);   // 与当前固件一致
    aw9523_set_pin(DEEPV1_AW_PIN_EPD_RESET, 0);               // 先按住复位
    aw9523_set_pin(DEEPV1_AW_PIN_EPD_POWER, 1);               // 再上电
    vTaskDelay(pdMS_TO_TICKS(500));
    ESP_LOGI(TAG, "POWER=1，RESET=0（复位保持）");
    trace_busy("复位保持中");

    aw9523_set_pin(DEEPV1_AW_PIN_EPD_RESET, 1);
    ESP_LOGI(TAG, "RESET 释放");
    trace_busy("复位释放后");

    // ── ③ 触发一次刷新，看 BUSY 有没有拉高 ────────────────────────────
    ESP_LOGI(TAG, "──── ③ 触发一次刷新 ────");
    memset(buf, 0x00, EPD_ARRAY);   // 全黑
    // 这个 API 发完整帧 + 0x22/0x20 但不等 BUSY，正好用来观察 BUSY 跳变。
    EPD_Dis_PartAll_Async(buf);
    trace_busy("0x20 触发后");

    // ── ④ 带上拉 + 充分上电延时，完整驱动一遍 ─────────────────────────
    ESP_LOGI(TAG, "──── ④ 带上拉完整驱动一遍 ────");
    gpio_set_pull_mode(DEEPV1_PIN_EPD_BUSY, GPIO_PULLUP_ONLY);
    aw9523_set_pin(DEEPV1_AW_PIN_EPD_POWER, 1);
    vTaskDelay(pdMS_TO_TICKS(500));

    memset(buf, 0x00, EPD_ARRAY);
    EPD_HW_Init();
    trace_busy("init 之后");
    EPD_WhiteScreen_ALL(buf);
    trace_busy("写帧之后");

    ESP_LOGI(TAG, ">>> 看屏有没有变黑，停留 10 秒 <<<");
    vTaskDelay(pdMS_TO_TICKS(10000));

    ESP_LOGI(TAG, "=== 结束，之后每秒打印一次 BUSY ===");
    for (;;) {
        ESP_LOGI(TAG, "idle: BUSY = %d", busy());
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
