/**
 * @file audio_service.h
 * @brief 系统级播放会话 —— 播放列表 + 当前位置 + 播放状态
 *
 * 为什么放在 system/ 而不是 apps/player/：
 *   app_manager 关闭一个 app 时会调它的 stop_func()。如果音频引擎挂在
 *   player app 的生命周期上，一退出播放器就断音 —— 而"边看书边听歌"恰恰
 *   是墨水屏设备最合理的用法。所以播放会话必须比 app 活得久：
 *   本服务在开机时随 clock / wifi 一起初始化，播放器 app 只是它的遥控器。
 *
 * 队列里存的是完整路径而不是"曲目库下标"，因为曲目库属于 app，
 * app 关掉之后下标就没有意义了。
 *
 * 所有函数都只能在 LVGL 线程调用（和其他 system 服务一致）。
 */

#ifndef AUDIO_SERVICE_H
#define AUDIO_SERVICE_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "audio_out.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AUDIO_QUEUE_MAX     32
#define AUDIO_PATH_MAX      160
#define AUDIO_TITLE_MAX     64

/* 播放模式。只影响"一首放完之后去哪"，不影响用户手动按上一首/下一首 ——
 * 手动切歌在任何模式下都应该老老实实换一首，包括单曲循环。 */
typedef enum {
    AUDIO_MODE_SEQUENTIAL = 0,  /* 顺序：放完最后一首就停 */
    AUDIO_MODE_REPEAT_ALL,      /* 列表循环 */
    AUDIO_MODE_REPEAT_ONE,      /* 单曲循环 */
    AUDIO_MODE_SHUFFLE,         /* 随机（一轮内不重复） */
    AUDIO_MODE_COUNT
} audio_mode_t;

typedef enum {
    AUDIO_STATE_STOPPED = 0,
    AUDIO_STATE_PLAYING,
    AUDIO_STATE_PAUSED,
} audio_state_t;

typedef struct {
    char path[AUDIO_PATH_MAX];
    char title[AUDIO_TITLE_MAX];
} audio_track_t;

/* ── 观察者 ───────────────────────────────────────────────────────────
 * UI 订阅这些事件来刷新自己。app 关闭时必须 unsubscribe，
 * 否则服务还活着、回调却指向已释放的页面。
 * （这也是不用 app_event 总线的原因：它只有 register 没有 unregister。） */

typedef enum {
    AUDIO_EV_TRACK_CHANGED,   /* 换曲了：标题、时长、进度全变 */
    AUDIO_EV_STATE_CHANGED,   /* 播放 / 暂停 / 停止 */
    AUDIO_EV_QUEUE_CHANGED,   /* 队列增删 */
    AUDIO_EV_OPTION_CHANGED,  /* 播放模式或音量变了 */
    AUDIO_EV_ERROR,           /* 打开或解码失败，err 有效 */
} audio_event_t;

typedef void (*audio_observer_t)(audio_event_t ev, audio_err_t err, void *user_data);

void audio_service_subscribe(audio_observer_t cb, void *user_data);
void audio_service_unsubscribe(audio_observer_t cb);

/* ── 生命周期：开机时调一次，和 time_service_init / wifi_manager_init 同级 ──
 * init() 会从 flash 恢复上次的队列、曲目和播放位置，但**不会自动开始播放**
 * （和主流播放器一致：恢复成暂停态，按一下播放键才从原位继续）。 */
esp_err_t audio_service_init(void);
void      audio_service_deinit(void);

/** 立刻把播放进度落盘。进入休眠 / 关机前调用。
 *  平时由服务自己在状态变化时写，不需要外部干预。 */
void      audio_service_checkpoint(void);

/* ── 队列 ─────────────────────────────────────────────────────────── */
void     audio_service_clear_queue(void);
bool     audio_service_enqueue(const char *path, const char *title);
uint16_t audio_service_queue_count(void);
const audio_track_t *audio_service_queue_at(uint16_t idx);
int16_t  audio_service_queue_pos(void);
bool     audio_service_remove_at(uint16_t idx);
/** 队列里是否已经有这个路径 */
bool     audio_service_queue_contains(const char *path);

/* ── 控制 ─────────────────────────────────────────────────────────── */
/** 从队列第 idx 首开始播 */
audio_err_t audio_service_play_index(uint16_t idx);
/** 单曲播放：清空队列只放这一首 */
audio_err_t audio_service_play_single(const char *path, const char *title);

void audio_service_toggle(void);
void audio_service_next(void);
void audio_service_prev(void);
void audio_service_stop(void);

void audio_service_seek_to(uint32_t pos_ms);
void audio_service_seek_by(int32_t delta_ms);

/* ── 查询 ─────────────────────────────────────────────────────────── */
audio_state_t audio_service_state(void);
uint32_t      audio_service_position_ms(void);
uint32_t      audio_service_duration_ms(void);
const audio_track_t *audio_service_current(void);
bool          audio_service_is_available(void);

/* ── 播放模式 ─────────────────────────────────────────────────────── */
audio_mode_t  audio_service_mode(void);
void          audio_service_set_mode(audio_mode_t mode);
void          audio_service_cycle_mode(void);
const char   *audio_mode_text(audio_mode_t mode);

/* ── 音量 ─────────────────────────────────────────────────────────────
 * 墨水屏上不要做连续滑块：拖动过程中每个中间值都是一次面板刷新。
 * 统一走离散档位（AUDIO_VOLUME_STEP），配 −/+ 两个按钮或物理音量键。 */
#define AUDIO_VOLUME_STEP  10
void          audio_service_set_volume(uint8_t vol);   /* 0-100 */
uint8_t       audio_service_volume(void);
void          audio_service_volume_up(void);
void          audio_service_volume_down(void);

/** 错误码 → 给用户看的中文文案 */
const char   *audio_err_text(audio_err_t err);

#ifdef __cplusplus
}
#endif

#endif /* AUDIO_SERVICE_H */
