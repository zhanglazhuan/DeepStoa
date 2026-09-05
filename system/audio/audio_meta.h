/**
 * @file audio_meta.h
 * @brief 音频文件元数据解析 —— 时长、比特率、ID3 标签
 *
 * 重要：**这一层不需要 codec，也不出声。**
 * ID3v2 标签、WAV 的 RIFF 头、MP3 的 Xing/Info 头，全都是纯文件格式解析。
 * 只要文件系统上有真实文件，这些今天就能做、也能验证 —— 时长算得对不对，
 * 对着文件就能看出来，不需要听见声音。
 *
 * 所以它放在 audio_out 之外、之上：mock 实现用它报告真实时长（进度条因此
 * 是真的），将来的 i2s 实现同样用它，不必重复解析。
 *
 * 只读文件头（最多 AUDIO_META_SCAN_BYTES），不整文件扫描 ——
 * 列表页扫目录时每首都要读一次，不能慢。
 */

#ifndef AUDIO_META_H
#define AUDIO_META_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AUDIO_META_TEXT_MAX     64
#define AUDIO_META_SCAN_BYTES   4096

typedef enum {
    AUDIO_FMT_UNKNOWN = 0,
    AUDIO_FMT_WAV,
    AUDIO_FMT_MP3,
} audio_fmt_t;

typedef struct {
    audio_fmt_t fmt;
    uint32_t    duration_ms;    /* 0 = 解析不出来 */
    uint32_t    bitrate_kbps;   /* VBR 时是平均值 */
    uint32_t    sample_rate;
    uint8_t     channels;
    bool        vbr;            /* 读到 Xing/Info 头 */
    uint32_t    size;           /* 文件字节数 */

    /* ID3v2 文本帧；没有标签时为空串，调用方回落到文件名 */
    char title[AUDIO_META_TEXT_MAX];
    char artist[AUDIO_META_TEXT_MAX];
    char album[AUDIO_META_TEXT_MAX];
} audio_meta_t;

/**
 * 读取元数据。
 * @return false = 文件打不开或格式无法识别（out 会被清零）
 *
 * 注意 duration_ms 可能是 0（比如没有 Xing 头的 VBR MP3 只能估算，
 * 估不出来时宁可给 0，也不给一个会让进度条乱飘的假值）。
 */
bool audio_meta_read(const char *path, audio_meta_t *out);

/** 供 UI 显示：有 ID3 标题就用「艺人 - 标题」，否则回落到文件名 */
void audio_meta_display_name(const audio_meta_t *m, const char *fallback,
                             char *out, size_t out_len);

#ifdef __cplusplus
}
#endif

#endif /* AUDIO_META_H */
