/**
 * @file alarm_service.h
 * @brief 系统级闹钟与倒计时服务 —— 闹钟表 + 计时器状态 + 持久化。
 *
 * 为什么放在 system/ 而不是 apps/clock：
 *   app_manager 关掉一个 app 时会调它的 stop_func()。原来闹钟检查和倒计时
 *   都是 Clock app 的 controller 里的 lv_timer，模型也在 stop 时被 free ——
 *   于是回到桌面计时器就停、退出 app 闹钟就不再检查。而“设完番茄钟去干别的”
 *   恰恰是这个功能唯一的用法。所以它必须比 app 活得久：开机随
 *   time_service / wifi 一起初始化，Clock app 只是它的遥控器。
 *   同样的道理见 system/audio/audio_service.h。
 *
 * 时间来源是 system/timeservice：闹钟不自己轮询 time()，而是订阅
 * APP_EVENT_CLOCK_TICK（每分钟一次）。SNTP 没对上时不触发 —— 宁可不响，
 * 也不要按一个从 1970 开始自由走的时钟去响。
 *
 * 到点时本服务负责震动，并广播 APP_EVENT_ALARM_FIRED /
 * APP_EVENT_TIMER_FINISHED；弹窗由 uilv 的 lv_alarm_alert 接管，
 * 服务本身不碰 UI。
 *
 * 所有函数只在 LVGL 线程调用（和其他 system 服务一致）。
 */

#ifndef SYS_ALARM_SERVICE_H
#define SYS_ALARM_SERVICE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ALARM_MAX_COUNT     19
#define ALARM_LABEL_LEN     32
#define ALARM_PRESET_COUNT  4

/* 重复位掩码：bit0 = 周日, bit1 = 周一 … bit6 = 周六 */
typedef enum {
    ALARM_REPEAT_NONE     = 0x00,
    ALARM_REPEAT_SUN      = 0x01,
    ALARM_REPEAT_MON      = 0x02,
    ALARM_REPEAT_TUE      = 0x04,
    ALARM_REPEAT_WED      = 0x08,
    ALARM_REPEAT_THU      = 0x10,
    ALARM_REPEAT_FRI      = 0x20,
    ALARM_REPEAT_SAT      = 0x40,
    ALARM_REPEAT_WEEKDAYS = 0x3E,
    ALARM_REPEAT_WEEKEND  = 0x41,
    ALARM_REPEAT_EVERY    = 0x7F,
} alarm_repeat_t;

typedef struct {
    uint8_t hour;                    /* 0-23 */
    uint8_t minute;                  /* 0-59 */
    uint8_t repeat;                  /* alarm_repeat_t 位掩码 */
    char    label[ALARM_LABEL_LEN];
    bool    enabled;
} alarm_t;

typedef enum {
    ALARM_TIMER_IDLE = 0,
    ALARM_TIMER_RUNNING,
    ALARM_TIMER_PAUSED,
    ALARM_TIMER_FINISHED,
} alarm_timer_state_t;

/* ── 生命周期 ───────────────────────────────────────────────────────── */

/** 开机调一次。必须在 flash_control_init() 和 time_service_init() 之后。 */
void alarm_service_init(void);

/** 恢复出厂：写回默认闹钟与预设。供 app_manager 的 factory_reset_func 调用。 */
bool alarm_service_factory_reset(void);

/* ── 闹钟 ───────────────────────────────────────────────────────────── */

uint8_t        alarm_service_count(void);

/** 越界返回 NULL。返回的指针指向服务内部数组，增删之后即失效。 */
const alarm_t *alarm_service_get(uint8_t idx);

/** 追加一条。返回新条目下标，满了返回 -1。 */
int  alarm_service_add(const alarm_t *a);

/** 覆盖第 idx 条（hour/minute/repeat/label，不动 enabled）。 */
bool alarm_service_update(uint8_t idx, const alarm_t *a);

bool alarm_service_remove(uint8_t idx);

/** 翻转启用状态并落盘。开关状态单独存一个位图，不重写整张闹钟表。
 *
 *  @return 操作是否成功（idx 越界为 false），语义与 update/remove 一致。
 *          翻转后的新状态用 alarm_service_get(idx)->enabled 回读，
 *          不要拿返回值当状态用。 */
bool alarm_service_toggle(uint8_t idx);

/**
 * @brief 下一个会响的闹钟。
 * @param out_idx     命中的下标（可为 NULL）
 * @param out_minutes 距离现在还有多少分钟（可为 NULL）
 * @return 没有启用的闹钟、或时间没对上时返回 false。
 */
bool alarm_service_next(uint8_t *out_idx, uint32_t *out_minutes);

/* ── 倒计时 ─────────────────────────────────────────────────────────── */

/** 设定时长并回到 IDLE。seconds 为 0 时不会启动。 */
void alarm_service_timer_set(uint32_t seconds);

uint32_t alarm_service_timer_set_seconds(void);

void alarm_service_timer_start(void);
void alarm_service_timer_pause(void);

/** 回到设定值并停止。 */
void alarm_service_timer_reset(void);

alarm_timer_state_t alarm_service_timer_state(void);

/** 剩余秒数（向上取整到秒，避免显示上少一秒）。 */
uint32_t alarm_service_timer_remaining_s(void);

uint8_t  alarm_service_timer_progress_pct(void);

/* ── 快捷预设（分钟） ───────────────────────────────────────────────── */

uint16_t alarm_service_preset(uint8_t i);

/** 写入全部 ALARM_PRESET_COUNT 个预设并落盘。 */
void     alarm_service_set_presets(const uint16_t *minutes);

#ifdef __cplusplus
}
#endif

#endif /* SYS_ALARM_SERVICE_H */
