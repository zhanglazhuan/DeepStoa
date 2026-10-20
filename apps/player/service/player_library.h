/**
 * @file player_library.h
 * @brief 曲目库 —— 扫描 SD 卡上的固定音乐目录
 *
 * 只读取 SD 卡上 PLAYER_MUSIC_DIR 中的实际音频文件。
 */

#ifndef PLAYER_LIBRARY_H
#define PLAYER_LIBRARY_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 固定的音乐目录。SD 卡挂载点也用同一个前缀，切真实数据时路径不用改。 */
#define PLAYER_MUSIC_DIR   "/sdcard/music"

#define PLAYER_MAX_TRACKS   64
#define PLAYER_TITLE_MAX    64
#define PLAYER_PATH_MAX     160

typedef struct {
    char     title[PLAYER_TITLE_MAX];   /* 文件名（含扩展名） */
    char     path[PLAYER_PATH_MAX];     /* 绝对路径 */
    uint32_t size;                      /* 字节 */
    uint32_t duration_ms;               /* 从文件头解析；0 = 未知 */
    bool     supported;                 /* 扩展名是否在支持列表里 */
} player_track_t;

/**
 * 扫描音乐目录。
 * @param out    调用方提供的数组
 * @param max    数组容量
 * @return 实际写入的曲目数
 *
 * 列出 .wav 和 .mp3 文件；MP3 在当前 WAV 解码器下标为不支持。
 */
uint16_t player_library_scan(player_track_t *out, uint16_t max);

/** 删除一个曲目文件。 */
bool player_library_delete(const char *path);

/** 扩展名是否可播 —— 实际由解码器（audio_out）说了算 */
bool player_library_ext_supported(const char *name);

#ifdef __cplusplus
}
#endif

#endif /* PLAYER_LIBRARY_H */
