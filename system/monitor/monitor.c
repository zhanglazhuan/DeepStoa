/**
 * @file monitor.c
 * @brief 运行时资源监控 — 定期打印堆(内部 DRAM + PSRAM)指标,持续超阈时记告警日志
 *
 * 移植自 D:\Codes\NomadCast\system\monitor，相对原版多了一个分配失败钩子，
 * 原因见 monitor.h 的模块注释。
 */
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "monitor.h"

static const char *TAG = "monitor";

/* 超阈持续检测状态 */
typedef struct {
    int64_t over_start_us;
    bool alerted;
} monitor_state_t;

static monitor_state_t s_monitor;

/* ── 分配失败记录 ────────────────────────────────────────────────────────
 * 刻意"只记不打"：分配失败往往不是孤立事件，而是一秒钟几千次的雪崩
 * （LVGL 渲染失败后会原地重试，正是这种形态）。在钩子里直接 ESP_LOGE
 * 会把串口刷爆，反过来让卡死更严重；而且钩子可能在中断上下文被调用，
 * 那里不能用普通 ESP_LOG。
 * 所以这里只往静态结构里记第一次的现场 + 累计次数，由周期报告负责输出。 */
typedef struct {
    volatile uint32_t count;          /* 累计失败次数 */
    volatile uint32_t reported;       /* 已经打印过的次数，用于只报增量 */
    size_t            first_size;     /* 第一次失败要多大 */
    uint32_t          first_caps;     /* 第一次失败要什么 caps */
    const char       *first_func;     /* 第一次失败的调用者（静态字符串，可直接存指针） */
    int64_t           first_us;
} alloc_fail_t;

static alloc_fail_t s_fail;

static void IRAM_ATTR alloc_failed_hook(size_t size, uint32_t caps, const char *function_name)
{
    if (s_fail.count == 0) {
        s_fail.first_size = size;
        s_fail.first_caps = caps;
        s_fail.first_func = function_name;   /* IDF 传的是 __func__，常量区字符串 */
        s_fail.first_us   = esp_timer_get_time();
    }
    s_fail.count++;
}

/* 把 caps 位翻译成人能读的形式 —— 光看 0x1804 判断不出来是内部还是 PSRAM */
static void caps_to_str(uint32_t caps, char *out, size_t out_len)
{
    snprintf(out, out_len, "%s%s%s%s0x%04X",
             (caps & MALLOC_CAP_INTERNAL) ? "INTERNAL|" : "",
             (caps & MALLOC_CAP_SPIRAM)   ? "SPIRAM|"   : "",
             (caps & MALLOC_CAP_DMA)      ? "DMA|"      : "",
             (caps & MALLOC_CAP_EXEC)     ? "EXEC|"     : "",
             (unsigned)caps);
}

static void report_alloc_failures(void)
{
    uint32_t count = s_fail.count;
    if (count == s_fail.reported) return;      /* 没有新增，不刷屏 */

    char capstr[64];
    caps_to_str(s_fail.first_caps, capstr, sizeof(capstr));

    ESP_LOGE(TAG, "ALLOC FAILED x%u (new since last report: %u). "
             "First: %u B caps=%s in %s() at %lld ms",
             (unsigned)count, (unsigned)(count - s_fail.reported),
             (unsigned)s_fail.first_size, capstr,
             s_fail.first_func ? s_fail.first_func : "?",
             (long long)(s_fail.first_us / 1000));

    s_fail.reported = count;
}

/* 检测是否超阈,超阈持续 MONITOR_ALERT_DURATION_SEC 才记一次告警 */
static void monitor_check_alert(int drm_pct, int ps_pct, size_t drm_free)
{
    bool over = (drm_pct >= MONITOR_DRAM_USED_PCT_THRESHOLD) ||
                (ps_pct  >= MONITOR_PSRAM_USED_PCT_THRESHOLD) ||
                (drm_free <= MONITOR_DRAM_MIN_FREE_THRESHOLD);

    if (over) {
        if (s_monitor.over_start_us == 0) {
            s_monitor.over_start_us = esp_timer_get_time();
        }
        int64_t elapsed_us = esp_timer_get_time() - s_monitor.over_start_us;
        if (!s_monitor.alerted && elapsed_us >= (int64_t)MONITOR_ALERT_DURATION_SEC * 1000000LL) {
            s_monitor.alerted = true;
            ESP_LOGW(TAG, "RESOURCE ALERT: DRAM=%d%% PSRAM=%d%% DRAM_free=%u B "
                     "(over threshold for >=%d s)",
                     drm_pct, ps_pct, (unsigned)drm_free, MONITOR_ALERT_DURATION_SEC);
        }
    } else {
        s_monitor.over_start_us = 0;
        s_monitor.alerted = false;
    }
}

void monitor_report(void)
{
    /* 堆:内部 DRAM(稀缺,~333KB) + PSRAM(富余,8MB) */
    size_t drm_total   = heap_caps_get_total_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    size_t drm_free    = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    size_t drm_min     = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    size_t drm_largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    /* DMA 可用内存是内部 RAM 的**子集**（MALLOC_CAP_DMA 只挂在内部 DRAM 区上，
     * PSRAM 堆没有这个 cap）。只看 INTERNAL 的 largest 会误判：见过 INTERNAL
     * largest=8192、却连 4096 的 DMA 缓冲都分配不出来的情况 —— 差的就是这一列。
     * SPI/I2S 这些外设从 PSRAM 发数据时要现场找内部 DMA 反弹缓冲，卡的就是它。 */
    size_t dma_free    = heap_caps_get_free_size(MALLOC_CAP_DMA);
    size_t dma_min     = heap_caps_get_minimum_free_size(MALLOC_CAP_DMA);
    size_t dma_largest = heap_caps_get_largest_free_block(MALLOC_CAP_DMA);

    size_t ps_total    = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    size_t ps_free     = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    size_t ps_min      = heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM);
    size_t ps_largest  = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);

    int drm_pct = drm_total ? (int)((drm_total - drm_free) * 100 / drm_total) : 0;
    int ps_pct  = ps_total  ? (int)((ps_total  - ps_free)  * 100 / ps_total)  : 0;

    /* largest 和 free 一起看才有意义：free 稳住但 largest 掉下去 = 碎片化；
     * 两个一起单调下降 = 真泄漏。只看 free 区分不出来。 */
    ESP_LOGI(TAG, "DRAM : used=%u/%u B (%d%%)  min_free=%u  largest=%u",
             (unsigned)(drm_total - drm_free), (unsigned)drm_total, drm_pct,
             (unsigned)drm_min, (unsigned)drm_largest);
    ESP_LOGI(TAG, "DMA  : free=%u  min_free=%u  largest=%u  (内部 RAM 的子集)",
             (unsigned)dma_free, (unsigned)dma_min, (unsigned)dma_largest);
    ESP_LOGI(TAG, "PSRAM: used=%u/%u B (%d%%)  min_free=%u  largest=%u",
             (unsigned)(ps_total - ps_free), (unsigned)ps_total, ps_pct,
             (unsigned)ps_min, (unsigned)ps_largest);

#if CONFIG_FREERTOS_USE_TRACE_FACILITY && CONFIG_FREERTOS_USE_STATS_FORMATTING_FUNCTIONS
    /* 任务栈高水位(vTaskList 格式:name/state/prio/stack-free(words)/task#)
     * 默认关着：trace facility 本身要占内部 RAM，而这台机器正缺这个。
     * 要查栈溢出时临时打开 CONFIG_FREERTOS_USE_TRACE_FACILITY +
     * CONFIG_FREERTOS_USE_STATS_FORMATTING_FUNCTIONS 重编即可。 */
    static char taskbuf[1024];
    vTaskList(taskbuf);
    ESP_LOGI(TAG, "Task list (stack-free in words):\n%s", taskbuf);
#endif

    report_alloc_failures();
    monitor_check_alert(drm_pct, ps_pct, drm_free);
}

static void monitor_timer_cb(void *arg)
{
    (void)arg;
    monitor_report();
}

void monitor_init(void)
{
    memset(&s_monitor, 0, sizeof(s_monitor));
    memset(&s_fail, 0, sizeof(s_fail));

    /* 装上分配失败钩子。这是把"静默卡死"变成"一行日志"的关键：
     * 报告跑在 esp_timer 任务上，即使 main 已经在 LVGL 里活锁，
     * 这边照样每 60 秒告诉你有多少次分配失败、第一次是谁要多大。 */
    esp_err_t err = heap_caps_register_failed_alloc_callback(alloc_failed_hook);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "failed-alloc hook not installed: %s", esp_err_to_name(err));
    }

    const esp_timer_create_args_t args = {
        .callback = monitor_timer_cb,
        .arg = NULL,
        .name = "monitor",
        .dispatch_method = ESP_TIMER_TASK,
    };
    esp_timer_handle_t timer = NULL;
    if (esp_timer_create(&args, &timer) == ESP_OK) {
        esp_timer_start_periodic(timer, (uint64_t)MONITOR_SAMPLE_INTERVAL_SEC * 1000000ULL);
        ESP_LOGI(TAG, "monitor started (every %d s)", MONITOR_SAMPLE_INTERVAL_SEC);
    } else {
        ESP_LOGE(TAG, "monitor timer create failed");
    }

    /* 立刻打一次基线：这一帧是"什么都还没起来时内部 RAM 有多少"，
     * 后面所有数字都要和它比才有意义。不打的话第一份数据要等 60 秒。 */
    monitor_report();
}
