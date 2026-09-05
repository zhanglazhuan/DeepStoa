/*
 * DeepStoa —— 系统时间服务
 *
 * 由 RTC + SNTP 维护的墙上时间。开机加载持久化的时区/格式，联网后自动对时。
 * 每分钟通过 app_event 广播一次 APP_EVENT_CLOCK_TICK，监听方调
 * app_event_register() 订阅 —— 不另设回调 API。
 *
 * 为什么叫 time_service 而不是 clock：
 *   ESP-IDF 的组件以目录名为键，本目录曾经叫 system/clock，和 apps/clock 撞名，
 *   构建时被静默丢弃（整个服务一行都没编进固件，状态栏因此只能显示假时间）。
 *   同时两边都导出了 clock_init()，改名不彻底会直接撞重复符号。
 *   所以目录、文件、函数前缀三处一起改成 time_service_*，别再改回去。
 *
 * 所有函数只在 LVGL 线程调用（和其他 system 服务一致）。
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── 生命周期 ───────────────────────────────────────────────────────────── */

/** 开机调一次。恢复时区与 12/24h 设置，建立 1 秒定时器（按分钟发 tick）。 */
void time_service_init(void);

/* ── 配置（由 Settings 调用） ───────────────────────────────────────────── */

/**
 * @brief 设置时区并立即生效。
 * @param posix_tz  POSIX TZ 字符串，如 "UTC-8"（注意符号与 UTC 偏移相反）。
 *
 * 会持久化到 NVS，下次开机在任何 app 启动之前就已经是对的时区。
 */
void time_service_set_timezone_posix(const char *posix_tz);

/** 设置 12h(false) / 24h(true) 显示格式，持久化并立即广播一次 tick。 */
void time_service_set_format_24h(bool fmt24);

bool time_service_get_format_24h(void);

/* ── 当前时间 ───────────────────────────────────────────────────────────── */

int  time_service_get_hour(void);     /* 已按 12/24h 设置换算 */
int  time_service_get_minute(void);
int  time_service_get_second(void);

/** 是否已经和 SNTP 对上时。没对上时不要把时间当真 —— 闹钟应拒绝触发。 */
bool time_service_is_synced(void);

/* ── SNTP ───────────────────────────────────────────────────────────────── */

/** 主动发起一次对时。WiFi 连上时会自动调，一般不需要手动调。 */
void time_service_sync_sntp(void);

#ifdef __cplusplus
}
#endif
