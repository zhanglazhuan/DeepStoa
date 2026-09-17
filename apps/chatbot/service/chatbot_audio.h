/**
 * @file chatbot_audio.h
 * @brief 语音采集抽象层 —— controller 只认这个接口，不认识 I2S
 *
 * 实现有两份，共用本头文件，由 CMakeLists 二选一编译：
 *   chatbot_audio_mock.c   当前阶段（没有麦克风电路）
 *   chatbot_audio_i2s.c    阶段 2，板子到位后补
 *
 * ── 实现方必须遵守的三条契约 ──────────────────────────────────────────
 *
 * 1. 回调必须在 LVGL 线程被调用。
 *    真实实现跑在 I2S 任务里，回调前必须用 lv_async_call() 弹回 UI 线程。
 *    在别的线程里碰 LVGL 对象会崩，而且崩得很难查。
 *
 * 2. chatbot_audio_stop() 之后必定回调一次，成功或失败都要回。
 *    实现内部要有兜底，绝不允许"不回调" —— 否则 controller 的状态机会
 *    永久卡在 STATE_RECORDING，用户只能重启 app。
 *
 * 3. 回调里传出的 clip 指针只在回调期间有效。
 *    调用方必须当场拷走需要的数据，实现方可以在回调返回后立刻释放。
 */

#ifndef CHATBOT_AUDIO_H
#define CHATBOT_AUDIO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 录音时长边界 */
#define CHATBOT_REC_MIN_MS   300u     /* 短于此判为误触，不发送 */
#define CHATBOT_REC_MAX_MS   60000u   /* 到点自动停止并发送，不丢弃 */

typedef enum {
    CHATBOT_AUDIO_OK = 0,
    CHATBOT_AUDIO_ERR_INIT,       /* 麦克风/I2S 初始化失败，设备级不可用 */
    CHATBOT_AUDIO_ERR_TOO_SHORT,  /* 时长 < CHATBOT_REC_MIN_MS */
    CHATBOT_AUDIO_ERR_SILENT,     /* 全程能量低于阈值 */
    CHATBOT_AUDIO_ERR_OVERFLOW,   /* 缓冲区溢出 / DMA 丢帧 / 内存不足 */
} chatbot_audio_err_t;

typedef struct {
    const void *pcm;          /* 16kHz 16bit mono；mock 实现为 NULL */
    size_t      len;
    uint32_t    duration_ms;
    uint8_t     peak_level;   /* 0-100，供 UI 显示音量格 */
} chatbot_audio_clip_t;

/** 录音结束回调（正常或异常）。保证在 LVGL 线程调用。 */
typedef void (*chatbot_audio_done_cb_t)(const chatbot_audio_clip_t *clip,
                                        chatbot_audio_err_t err,
                                        void *user_data);

esp_err_t chatbot_audio_init(void);
void      chatbot_audio_deinit(void);

/** false → UI 必须禁用录音按钮并显示 L3 横幅，引导用户改用键盘输入 */
bool      chatbot_audio_is_available(void);

esp_err_t chatbot_audio_start(void);
esp_err_t chatbot_audio_stop(chatbot_audio_done_cb_t cb, void *user_data);

/** 丢弃本次录音，不回调（用户取消 / 手指滑出按钮） */
void      chatbot_audio_cancel(void);

uint32_t  chatbot_audio_elapsed_ms(void);  /* UI 1Hz 轮询显示计时 */
uint8_t   chatbot_audio_level(void);       /* 0-100，UI 画离散音量格 */

/* ── 仅 mock 实现提供：故障注入 ──────────────────────────────────────
 * 真实实现里这两个函数是空的。UI 调试时用来逐条走通错误分支。 */
void      chatbot_audio_mock_inject(chatbot_audio_err_t err);
void      chatbot_audio_mock_set_available(bool available);

#ifdef __cplusplus
}
#endif

#endif /* CHATBOT_AUDIO_H */
