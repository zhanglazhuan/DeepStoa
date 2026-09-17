#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"

#include "app.h"
#include "controller.h"
#include "view.h"
#include "model.h"
#include "audio_service.h"
#include "service/player_library.h"

static const char *TAG = "player_controller";

static PlayerApp *s_app;

static PlayerModel *mdl(void)
{
    return (s_app && s_app->model) ? s_app->model : NULL;
}

void player_controller_rescan(void)
{
    PlayerModel *m = mdl();
    if (m) player_model_rescan(m);
}

bool player_controller_delete_track(uint16_t track_idx)
{
    PlayerModel *m = mdl();
    if (!m || track_idx >= m->track_count) return false;

    TrackInfo *t = &m->tracks[track_idx];

    /* 先从播放队列里摘掉，否则队列会指向一个已经不存在的文件 */
    for (uint16_t i = audio_service_queue_count(); i > 0; i--) {
        const audio_track_t *q = audio_service_queue_at(i - 1);
        if (q && strcmp(q->path, t->filepath) == 0) {
            audio_service_remove_at(i - 1);
        }
    }

    if (!player_library_delete(t->filepath)) {
        ESP_LOGE(TAG, "delete failed: %s", t->filepath);
        return false;
    }
    ESP_LOGI(TAG, "deleted %s", t->filepath);

    player_model_remove_track(m, track_idx);
    return true;
}

uint16_t player_controller_enqueue_selected(void)
{
    PlayerModel *m = mdl();
    if (!m) return 0;

    uint16_t added = 0;
    for (uint16_t i = 0; i < m->track_count; i++) {
        if (!m->tracks[i].selected) continue;
        if (!m->tracks[i].supported) continue;      /* 不支持的不入队 */
        if (audio_service_enqueue(m->tracks[i].filepath, m->tracks[i].title)) added++;
    }
    player_model_clear_selection(m);

    ESP_LOGI(TAG, "enqueued %u (queue=%u)", added, audio_service_queue_count());
    return added;
}

audio_err_t player_controller_play_track(uint16_t track_idx)
{
    PlayerModel *m = mdl();
    if (!m || track_idx >= m->track_count) return AUDIO_ERR_NOT_FOUND;

    /* 格式不支持的直接返回，让 view 弹窗，不改动播放状态 */
    if (!m->tracks[track_idx].supported) return AUDIO_ERR_FORMAT;

    /* 已经在队列里就跳到那一首，保留整个队列；否则单曲播放 */
    for (uint16_t i = 0; i < audio_service_queue_count(); i++) {
        const audio_track_t *q = audio_service_queue_at(i);
        if (q && strcmp(q->path, m->tracks[track_idx].filepath) == 0) {
            return audio_service_play_index(i);
        }
    }
    return audio_service_play_single(m->tracks[track_idx].filepath,
                                     m->tracks[track_idx].title);
}

void player_controller_init(struct PlayerApp *app) {
    ESP_LOGI(TAG, "player_controller_init");
    app->controller = malloc(sizeof(PlayerController));
    if (!app->controller) {
        ESP_LOGE(TAG, "Failed to allocate memory for PlayerController");
        return;
    }
    memset(app->controller, 0, sizeof(PlayerController));
    s_app = app;
}

void player_controller_deinit(struct PlayerApp *app) {
    ESP_LOGI(TAG, "player_controller_deinit");

    /* 关键：不要 stop 音频。播放会话属于 system/audio，
     * 退出播放器 app 之后音乐要继续放。 */

    if (app->controller) {
        free(app->controller);
        app->controller = NULL;
    }
    s_app = NULL;
}
