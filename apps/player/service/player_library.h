/**
 * @file player_library.h
 * @brief 曲目库 —— 扫描 SD 卡上的固定音乐目录
 *
 * 数据源自动切换：PLAYER_MUSIC_DIR 所在的文件系统已挂载就读真实目录
 * （走已有的 sd_control_get_dir_list），没挂载就回落到内置 mock 列表。
 * 两种情况下 UI 完全一致，所以现在没有 SD 卡也能调试整条交互链路。
 *
 * SD 卡到位后，把 mock 那段删掉即可，view / controller 不用改。
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
 * 会把不支持的格式也列出来（supported=false），这样用户能看到文件确实在，
 * 只是放不了 —— 比直接隐藏更好排查。
 */
uint16_t player_library_scan(player_track_t *out, uint16_t max);

/** 当前列出的是内置 mock 数据（true）还是真实文件系统（false） */
bool player_library_using_mock(void);

/** 删除一个曲目文件。mock 下只是从内存列表里移除。 */
bool player_library_delete(const char *path);

/** 扩展名是否可播 —— 实际由解码器（audio_out）说了算 */
bool player_library_ext_supported(const char *name);

#ifdef __cplusplus
}
#endif

#endif /* PLAYER_LIBRARY_H */
