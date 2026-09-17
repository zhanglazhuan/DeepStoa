#ifndef MONITOR_H
#define MONITOR_H

/**
 * @file monitor.h
 * @brief 运行时资源监控 —— 周期打印堆指标 + 分配失败钩子
 *
 * 从 D:\Codes\NomadCast\system\monitor 移植。
 *
 * 为什么这个模块对 DeepStoa 特别重要：本机静态 DIRAM 已经占到 97.35%
 * （idf.py size：332687/341760，只剩 9073 字节），内部 RAM 常年在悬崖边上。
 * 之前出过一次现象是"静默卡死"的故障：LVGL 在渲染里分配失败，而它的策略是
 * "Allocating layer buffer failed. Try later"（lv_draw.c:505）—— 返回 NULL
 * 但 draw task 不销毁，refr_invalid_areas 死等，lv_timer_handler() 再不返回，
 * IDLE0 饿死，最后只看到 task_wdt 每 5 秒报一次，串口上没有任何线索。
 *
 * 所以相比 NomadCast 的原版，这里多了一个分配失败钩子（见 monitor.c）：
 * 任何一次 heap 分配失败都会被记下大小/caps/调用者，由周期报告打出来。
 * 关键点是监控跑在 esp_timer 任务上，**main 卡死时它照常出报告**。
 */

#ifdef __cplusplus
extern "C" {
#endif

/* ── 采样 / 告警阈值配置 ─────────────────────────────────────────────────── */
#define MONITOR_SAMPLE_INTERVAL_SEC        60        /* 采样间隔(秒) */
#define MONITOR_DRAM_USED_PCT_THRESHOLD    80        /* 内部 DRAM 使用率告警阈值(%) */
#define MONITOR_PSRAM_USED_PCT_THRESHOLD   80        /* PSRAM 使用率告警阈值(%) */
#define MONITOR_DRAM_MIN_FREE_THRESHOLD    (32*1024) /* 内部 DRAM 剩余告警阈值(字节) */
#define MONITOR_ALERT_DURATION_SEC         30        /* 超阈持续该时长才记告警日志(秒) */

/**
 * 启动运行时监控：安装分配失败钩子，立刻打一次基线，之后按
 * MONITOR_SAMPLE_INTERVAL_SEC 周期打印指标并做阈值告警。
 *
 * 越早调用越好 —— 基线那一帧就是"什么都还没起来时内部 RAM 有多少"。
 */
void monitor_init(void);

/** 立即打印一次指标并检测阈值(可手动触发)。 */
void monitor_report(void);

#ifdef __cplusplus
}
#endif

#endif
