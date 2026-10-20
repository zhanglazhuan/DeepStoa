#include <stdio.h>
#include <string.h>
#include <strings.h>
#include "esp_log.h"

#include "player_library.h"
#include "sd_control.h"
#include "audio_out.h"
#include "audio_meta.h"

static const char *TAG = "player_library";

static bool is_audio_file(const char *name)
{
    const char *dot = name ? strrchr(name, '.') : NULL;
    return dot && (strcasecmp(dot, ".mp3") == 0 || strcasecmp(dot, ".wav") == 0);
}

/* 支持哪些格式由解码器说了算，这里直接问 audio_out，
 * 避免"列表页说能播、真播的时候报格式错"这种不一致。 */
bool player_library_ext_supported(const char *name)
{
    return audio_out_ext_supported(name);
}

uint16_t player_library_scan(player_track_t *out, uint16_t max)
{
    if (!out || max == 0 || !sd_control_is_ready(PLAYER_MUSIC_DIR)) return 0;

    file_list_t list = {0};
    if (!sd_control_get_dir_list(PLAYER_MUSIC_DIR, &list, NULL)) {
        ESP_LOGW(TAG, "read dir failed: %s", PLAYER_MUSIC_DIR);
        return 0;
    }

    uint16_t n = 0;
    for (uint32_t i = 0; i < list.count && n < max; i++) {
        if (list.nodes[i].type != NODE_TYPE_FILE || !is_audio_file(list.nodes[i].name)) continue;

        memset(&out[n], 0, sizeof(out[n]));
        int written = snprintf(out[n].path, sizeof(out[n].path), "%s/%s",
                               PLAYER_MUSIC_DIR, list.nodes[i].name);
        if (written < 0 || (size_t)written >= sizeof(out[n].path)) {
            ESP_LOGW(TAG, "skip audio file with overlong path");
            continue;
        }
        file_stat_t stat = {0};
        if (sd_control_file_get_stat(out[n].path, &stat)) {
            out[n].size = (uint32_t)stat.size;
        }
        out[n].supported = player_library_ext_supported(list.nodes[i].name);

        /* 标题优先用 ID3，没有标签才回落到文件名。
         * 只读文件头 4KB，所以扫一遍目录不会慢到不可接受；
         * 曲目库容量由 PLAYER_MAX_TRACKS 限制。 */
        audio_meta_t meta;
        bool have = out[n].supported && audio_meta_read(out[n].path, &meta);
        audio_meta_display_name(have ? &meta : NULL, list.nodes[i].name,
                                out[n].title, PLAYER_TITLE_MAX);
        if (have && meta.duration_ms > 0) out[n].duration_ms = meta.duration_ms;
        n++;
    }
    sd_control_free_dir_list(&list);
    ESP_LOGI(TAG, "scan %s -> %u tracks", PLAYER_MUSIC_DIR, n);
    return n;
}

bool player_library_delete(const char *path)
{
    if (!path) return false;

    return sd_control_delete(path);
}
