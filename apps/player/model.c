#include <string.h>
#include <stdlib.h>
#include "esp_log.h"

#include "app.h"
#include "model.h"

static const char *TAG = "player_model";

void player_model_init(PlayerApp* app) {
    app->model = malloc(sizeof(PlayerModel));
    if (!app->model) {
        ESP_LOGE(TAG, "Failed to allocate memory for PlayerModel");
        return;
    }
    memset(app->model, 0, sizeof(PlayerModel));

    player_model_rescan(app->model);
    ESP_LOGI(TAG, "PlayerModel initialized: %u tracks", app->model->track_count);
}

void player_model_deinit(PlayerApp* app) {
    if (app->model) {
        free(app->model);
        app->model = NULL;
    }
    ESP_LOGI(TAG, "PlayerModel deinitialized");
}

void player_model_rescan(PlayerModel *model)
{
    if (!model) return;

    /* 记住原来选中了哪些路径，扫描后按路径恢复 */
    static char kept[MAX_TRACKS][PLAYER_PATH_MAX];
    uint16_t kept_n = 0;
    for (uint16_t i = 0; i < model->track_count && kept_n < MAX_TRACKS; i++) {
        if (model->tracks[i].selected) {
            strncpy(kept[kept_n], model->tracks[i].filepath, PLAYER_PATH_MAX - 1);
            kept[kept_n][PLAYER_PATH_MAX - 1] = '\0';
            kept_n++;
        }
    }

    player_track_t scan[MAX_TRACKS];
    uint16_t n = player_library_scan(scan, MAX_TRACKS);

    memset(model->tracks, 0, sizeof(model->tracks));
    for (uint16_t i = 0; i < n; i++) {
        strncpy(model->tracks[i].title, scan[i].title, PLAYER_TITLE_MAX - 1);
        strncpy(model->tracks[i].filepath, scan[i].path, PLAYER_PATH_MAX - 1);
        model->tracks[i].size        = scan[i].size;
        model->tracks[i].duration_ms = scan[i].duration_ms;
        model->tracks[i].supported   = scan[i].supported;

        for (uint16_t k = 0; k < kept_n; k++) {
            if (strcmp(kept[k], scan[i].path) == 0) {
                model->tracks[i].selected = true;
                break;
            }
        }
    }
    model->track_count = n;
}

TrackInfo *player_model_find_by_path(PlayerModel *model, const char *path)
{
    if (!model || !path) return NULL;
    for (uint16_t i = 0; i < model->track_count; i++) {
        if (strcmp(model->tracks[i].filepath, path) == 0) return &model->tracks[i];
    }
    return NULL;
}

uint16_t player_model_selected_count(PlayerModel *model)
{
    if (!model) return 0;
    uint16_t n = 0;
    for (uint16_t i = 0; i < model->track_count; i++) {
        if (model->tracks[i].selected) n++;
    }
    return n;
}

void player_model_clear_selection(PlayerModel *model)
{
    if (!model) return;
    for (uint16_t i = 0; i < model->track_count; i++) model->tracks[i].selected = false;
}

bool player_model_remove_track(PlayerModel *model, uint16_t track_idx)
{
    if (!model || track_idx >= model->track_count) return false;

    memmove(&model->tracks[track_idx], &model->tracks[track_idx + 1],
            sizeof(TrackInfo) * (model->track_count - track_idx - 1));
    model->track_count--;
    memset(&model->tracks[model->track_count], 0, sizeof(TrackInfo));
    return true;
}
