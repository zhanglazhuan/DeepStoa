#ifndef PLAYER_CONTROLLER_H
#define PLAYER_CONTROLLER_H

#include <lvgl.h>
#include "model.h"
#include "audio_service.h"

/* 播放控制本身都在 system/audio 的 audio_service 里，UI 直接调它即可。
 * 本 controller 只保留"曲目库"相关、需要 app 上下文的操作。 */

struct PlayerApp;
struct PlayerView;

typedef struct PlayerController {
    PlayerModel *model;
    struct PlayerView *view;
} PlayerController;

void player_controller_init(struct PlayerApp *app);
void player_controller_deinit(struct PlayerApp *app);

void player_controller_rescan(void);

/** 删除曲目：先从播放队列摘掉，再删文件，最后从曲目库移除 */
bool player_controller_delete_track(uint16_t track_idx);

/** 把选中的曲目加入播放队列（跳过不支持的），返回新增条数 */
uint16_t player_controller_enqueue_selected(void);

/** 单曲播放。格式不支持时返回 AUDIO_ERR_FORMAT，由 view 弹窗。 */
audio_err_t player_controller_play_track(uint16_t track_idx);

#endif // PLAYER_CONTROLLER_H
