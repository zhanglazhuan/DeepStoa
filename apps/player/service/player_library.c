#include <stdio.h>
#include <string.h>
#include <strings.h>
#include "esp_log.h"

#include "player_library.h"
#include "sd_control.h"
#include "audio_out.h"
#include "audio_meta.h"

static const char *TAG = "player_library";

/* ── mock 曲目表 ───────────────────────────────────────────────────
 * 板子上还没有 SD 卡，工程里也没挂任何文件系统，所以默认走这张表。
 * 故意混入一个 .flac 和一个 .txt，用来验证「格式不支持」的弹窗。
 * SD 卡到位后整段删掉，player_library_scan() 会自动改读真实目录。 */

typedef struct { const char *name; uint32_t size; } mock_track_t;

static const mock_track_t k_mock[] = {
    {"01 Mockingbird.mp3",       4210000},
    {"02 Zephyr Theme.wav",     18900000},
    {"03 LVGL Beat.mp3",         3870000},
    {"04 Ink Refresh.mp3",       5120000},
    {"05 Partial Window.wav",   12400000},
    {"06 Lossless Demo.flac",   28700000},   /* 不支持 */
    {"readme.txt",                   1200},  /* 不支持 */
};
#define MOCK_COUNT ((uint16_t)(sizeof(k_mock) / sizeof(k_mock[0])))

/* mock 下的删除：只标记，不落盘 */
static bool s_mock_deleted[MOCK_COUNT];

bool player_library_using_mock(void)
{
    return !sd_control_is_ready(PLAYER_MUSIC_DIR);
}

/* 支持哪些格式由解码器说了算，这里直接问 audio_out，
 * 避免"列表页说能播、真播的时候报格式错"这种不一致。 */
bool player_library_ext_supported(const char *name)
{
    return audio_out_ext_supported(name);
}

static uint16_t scan_mock(player_track_t *out, uint16_t max)
{
    uint16_t n = 0;
    for (uint16_t i = 0; i < MOCK_COUNT && n < max; i++) {
        if (s_mock_deleted[i]) continue;

        strncpy(out[n].title, k_mock[i].name, PLAYER_TITLE_MAX - 1);
        out[n].title[PLAYER_TITLE_MAX - 1] = '\0';
        snprintf(out[n].path, PLAYER_PATH_MAX, "%s/%s", PLAYER_MUSIC_DIR, k_mock[i].name);
        out[n].size      = k_mock[i].size;
        out[n].supported = player_library_ext_supported(k_mock[i].name);
        n++;
    }
    return n;
}

static uint16_t scan_real(player_track_t *out, uint16_t max)
{
    file_list_t list = {0};
    if (!sd_control_get_dir_list(PLAYER_MUSIC_DIR, &list, NULL)) {
        ESP_LOGW(TAG, "read dir failed: %s", PLAYER_MUSIC_DIR);
        return 0;
    }

    uint16_t n = 0;
    for (uint32_t i = 0; i < list.count && n < max; i++) {
        if (list.nodes[i].type == NODE_TYPE_DIR) continue;   /* 只列文件 */

        /* sd_control 的 name 字段是 255 字节，这里必须限长，否则可能截断 */
        snprintf(out[n].path, PLAYER_PATH_MAX, "%s/%.*s", PLAYER_MUSIC_DIR,
                 (int)(PLAYER_PATH_MAX - sizeof(PLAYER_MUSIC_DIR) - 1), list.nodes[i].name);
        out[n].size      = (uint32_t)list.nodes[i].size;
        out[n].supported = player_library_ext_supported(list.nodes[i].name);

        /* 标题优先用 ID3，没有标签才回落到文件名。
         * 只读文件头 4KB，所以扫一遍目录不会慢到不可接受；
         * 真实 SD 上曲目上百之后要改成懒加载。 */
        audio_meta_t meta;
        bool have = out[n].supported && audio_meta_read(out[n].path, &meta);
        audio_meta_display_name(have ? &meta : NULL, list.nodes[i].name,
                                out[n].title, PLAYER_TITLE_MAX);
        if (have && meta.duration_ms > 0) out[n].duration_ms = meta.duration_ms;
        n++;
    }
    sd_control_free_dir_list(&list);
    return n;
}

uint16_t player_library_scan(player_track_t *out, uint16_t max)
{
    if (!out || max == 0) return 0;

    uint16_t n = player_library_using_mock() ? scan_mock(out, max) : scan_real(out, max);
    ESP_LOGI(TAG, "scan %s -> %u tracks (%s)", PLAYER_MUSIC_DIR, n,
             player_library_using_mock() ? "MOCK" : "real fs");
    return n;
}

bool player_library_delete(const char *path)
{
    if (!path) return false;

    if (!player_library_using_mock()) {
        return sd_control_delete(path);
    }

    /* mock：按文件名标记删除 */
    const char *name = strrchr(path, '/');
    name = name ? name + 1 : path;
    for (uint16_t i = 0; i < MOCK_COUNT; i++) {
        if (strcmp(k_mock[i].name, name) == 0) {
            s_mock_deleted[i] = true;
            ESP_LOGI(TAG, "mock delete: %s", name);
            return true;
        }
    }
    return false;
}
