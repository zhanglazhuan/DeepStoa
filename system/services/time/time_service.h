/*
 * DeepStoa —— 系统时间服务
 *
 * 由持久化时间锚点、开机单调时钟和 SNTP 维护的墙上时间。
 * 每分钟通过 app_event 广播一次 APP_EVENT_CLOCK_TICK，监听方调
 * app_event_register() 订阅 —— 不另设回调 API。
 *
 * 组件目录为 system/services/time，组件名为 time，避免与 apps/clock 冲突。
 * 对外函数仍使用 time_service_* 前缀，现有调用方和持久化数据无需迁移。
 *
 * 所有函数只在 LVGL 线程调用（和其他 system 服务一致）。
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    TIME_STATE_INVALID = 0,
    TIME_STATE_ESTIMATED,
    TIME_STATE_TRUSTED,
} time_service_state_t;

/* ── 生命周期 ───────────────────────────────────────────────────────────── */

/**
 * 可选的板级 RTC 后端。
 *
 * RTC 驱动只负责寄存器和 BCD，不拥有系统时间策略；time_service 仍然是
 * 唯一的时间源协调者。board 层在确认实际接线后，可在 time_service_init()
 * 前注册它。未注册时继续使用原有的系统时钟/SNTP 路径。
 */
typedef struct {
    bool (*read_utc)(time_t *out_utc, void *user_data);
    bool (*write_utc)(time_t utc, void *user_data);
    void *user_data;
} time_service_rtc_backend_t;

/** 注册可选 RTC 后端；传 NULL 可撤销。不会创建 I2C 总线。 */
void time_service_set_rtc_backend(const time_service_rtc_backend_t *backend);

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

/** 本次开机是否已有 SNTP 或用户手动确认的可信时间。 */
bool time_service_is_synced(void);

/** Current confidence of the wall clock. */
time_service_state_t time_service_get_state(void);

/** True when a trusted source attempted to move behind persisted study time. */
bool time_service_has_conflict(void);

/**
 * Return a non-decreasing epoch used by applications. INVALID returns false.
 * ESTIMATED advances from the last persisted safe time using boot uptime.
 */
bool time_service_now(uint32_t *epoch);

/** Monotonic milliseconds since boot, suitable for elapsed-duration limits. */
uint64_t time_service_monotonic_ms(void);

/** Persist a time already used in durable application data. */
void time_service_checkpoint(uint32_t epoch);

/** Set a user-confirmed local date/time epoch and mark this boot trusted. */
bool time_service_set_manual(uint32_t epoch);

/* ── SNTP ───────────────────────────────────────────────────────────────── */

/** 主动发起一次对时。WiFi 连上时会自动调，一般不需要手动调。 */
void time_service_sync_sntp(void);

#ifdef __cplusplus
}
#endif
