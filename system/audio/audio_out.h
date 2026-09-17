/**
 * @file audio_out.h
 * @brief 音频输出驱动抽象 —— 只描述"一个能出声的设备"
 *
 * 上层是 audio_service（系统级播放会话），它才是 app 打交道的对象。
 * 本层不知道播放列表、不知道 UI，只负责打开一个文件、出声、报位置。
 *
 * 实现有两份，共用本头文件，由 CMakeLists 二选一编译：
 *   audio_out_mock.c   当前阶段（板子上没有喇叭 / codec）
 *   audio_out_i2s.c    硬件到位后补
 *
 * ── 实现方必须遵守的契约 ──────────────────────────────────────────────
 *
 * 1. 所有回调必须在 LVGL 线程被调用。真实实现跑在解码任务里，
 *    回调前必须 lv_async_call() 弹回 UI 线程。
 *
 * 2. audio_out_position_ms() 会被 1Hz 轮询，必须无锁、快速返回，
 *    不能阻塞在 I2S 或文件读上。
 *
 * 3. audio_out_open() 要在真正解码前先校验格式，格式不对立刻返回
 *    AUDIO_ERR_FORMAT —— UI 的格式弹窗靠这个，不能等播到一半才报。
 *
 * 4. audio_out_ext_supported() 返回的集合必须和 open() 的实际能力一致。
 *    这是实现相关的：mock 支持 .mp3/.wav，真实解码器可能更多或更少。
 */

#ifndef AUDIO_OUT_H
#define AUDIO_OUT_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    AUDIO_OK = 0,
    AUDIO_ERR_INIT,       /* codec / I2S 初始化失败，设备级不可用 */
    AUDIO_ERR_NOT_FOUND,  /* 文件不存在（可能 SD 卡被拔了） */
    AUDIO_ERR_FORMAT,     /* 后缀或文件头不是支持的格式 */
    AUDIO_ERR_CORRUPT,    /* 能识别格式但解码失败 */
    AUDIO_ERR_IO,         /* 读取中途失败 */
} audio_err_t;

/** 播放自然结束（不是被 stop 打断）。保证在 LVGL 线程调用。 */
typedef void (*audio_out_eot_cb_t)(void *user_data);

/** 播放过程中出错。保证在 LVGL 线程调用。 */
typedef void (*audio_out_err_cb_t)(audio_err_t err, void *user_data);

esp_err_t audio_out_init(void);
void      audio_out_deinit(void);

/** false → UI 禁用播放控制并提示喇叭不可用 */
bool      audio_out_is_available(void);

void      audio_out_set_callbacks(audio_out_eot_cb_t on_eot,
                                  audio_out_err_cb_t on_err,
                                  void *user_data);

/** 本实现支持的扩展名（含点，如 ".mp3"）；用于列表页提前标记不可播的文件 */
bool      audio_out_ext_supported(const char *name_or_path);

/**
 * 打开并开始播放。同步完成格式校验后立即返回。
 * @param out_duration_ms 成功时写入总时长；失败时不写
 */
audio_err_t audio_out_open(const char *path, uint32_t *out_duration_ms);

void      audio_out_pause(void);
void      audio_out_resume(void);
void      audio_out_stop(void);

/** 跳到指定位置，会被夹到 [0, duration] */
void      audio_out_seek(uint32_t pos_ms);

uint32_t  audio_out_position_ms(void);
uint32_t  audio_out_duration_ms(void);
bool      audio_out_is_playing(void);

void      audio_out_set_volume(uint8_t vol);   /* 0-100 */
uint8_t   audio_out_volume(void);

/* ── 仅 mock 实现提供：故障注入 ────────────────────────────────────── */
void      audio_out_mock_inject(audio_err_t err);
void      audio_out_mock_set_available(bool available);

#ifdef __cplusplus
}
#endif

#endif /* AUDIO_OUT_H */
