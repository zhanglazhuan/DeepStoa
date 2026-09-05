#ifndef PLAYER_MODEL_H
#define PLAYER_MODEL_H

#include <stdint.h>
#include <stdbool.h>

#include "service/player_library.h"

/* 注意：播放列表、播放状态、播放位置都不在这里 —— 它们属于
 * system/audio 的 audio_service，比这个 app 活得久。
 * 本 model 只负责"曲目库"这个 app 生命周期内的东西。 */

#define MAX_TRACKS      PLAYER_MAX_TRACKS
#define MAX_PATH_LEN    PLAYER_PATH_MAX

typedef struct TrackInfo {
    char     title[PLAYER_TITLE_MAX];
    char     filepath[PLAYER_PATH_MAX];
    uint32_t size;
    uint32_t duration_ms;   /* 从文件头解析；0 = 未知 */
    bool     supported;   /* 扩展名不在解码器支持列表里 → 点开时弹窗 */
    bool     selected;    /* 列表页多选 */
} TrackInfo;

typedef struct PlayerModel {
    TrackInfo tracks[MAX_TRACKS];
    uint16_t  track_count;
} PlayerModel;

struct PlayerApp;
typedef struct PlayerApp PlayerApp;

void player_model_init(PlayerApp* app);
void player_model_deinit(PlayerApp* app);

/** 重新扫描音乐目录，保留仍然存在的曲目的选中状态 */
void player_model_rescan(PlayerModel *model);

/** 按路径找曲目库里的条目（用来标记"正在播放的是哪一行"） */
TrackInfo *player_model_find_by_path(PlayerModel *model, const char *path);

void     player_model_clear_selection(PlayerModel *model);
uint16_t player_model_selected_count(PlayerModel *model);

/** 从曲目库里移除一条（调用方负责先删文件、先从播放队列里摘掉） */
bool player_model_remove_track(PlayerModel *model, uint16_t track_idx);

#endif // PLAYER_MODEL_H
