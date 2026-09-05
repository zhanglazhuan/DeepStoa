/**
 * @file lv_folder_selector.c
 * @brief 文件/文件夹选择器页面实现
 *
 * Ported from D:\Codes\EPOS\epos\epos_lv\widgets\lv_folder_selector.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "esp_log.h"
#include "lv_page.h"
#include "lv_theme_hardcore.h"
#include "lv_folder_selector.h"
#include "sd_control.h"

static const char *TAG = "lv_folder_selector";

#define FSEL_MAX_ENTRIES   64    /* 单个目录一次最多列出的条目数 */
#define FSEL_MAX_NAME      64

/* ── 目录条目（数据源无关） ──────────────────────────────────────────── */

typedef struct {
    char name[FSEL_MAX_NAME];
    bool is_dir;
} fsel_entry_t;

/* ── 页面上下文 ─────────────────────────────────────────────────────── */

typedef struct {
    fs_selector_mode_t mode;
    char        current_path[FS_MAX_PATH_LEN];
    const char *filter_ext;              /* 静态字符串，不拷贝 */

    lv_obj_t   *path_label;
    lv_obj_t   *path_list;
    lv_obj_t   *hint_label;

    /* 当前目录的条目快照。列表按钮的 user_data 存的是这里的下标，
     * 避免像 EPOS 那样从 label 文本里反解析文件名。 */
    fsel_entry_t entries[FSEL_MAX_ENTRIES];
    uint16_t     entry_count;

    /* 选中状态 */
    char        selected_paths[FS_MAX_SELECTIONS][FS_MAX_PATH_LEN];
    uint8_t     selected_count;
    const char *path_ptrs[FS_MAX_SELECTIONS];

    fs_selection_t     final_selection;
    file_selector_cb_t callback;
    void              *user_data;
} fs_selector_ctx_t;

static void refresh_path_list(fs_selector_ctx_t *ctx);

/* ── 数据源：内置 mock 树 ───────────────────────────────────────────────
 * 当前板子没有 SD 卡、工程里也没有挂载任何文件系统，所以默认走这张表。
 * 一旦 FSEL_ROOT 上挂了真实文件系统，fsel_list_dir() 会自动改读真实目录，
 * 这张表和下面的 mock_* 函数就可以整段删掉，UI 代码一行都不用动。
 * ------------------------------------------------------------------- */

typedef struct {
    const char *parent;   /* 所属目录（绝对路径） */
    const char *name;
    bool        is_dir;
} mock_entry_t;

static const mock_entry_t mock_vfs[] = {
    {FSEL_ROOT,                  "decks",            true },
    {FSEL_ROOT,                  "notes",            true },
    {FSEL_ROOT,                  "hsk1.csv",         false},
    {FSEL_ROOT,                  "sample_cards.txt", false},
    {FSEL_ROOT "/decks",         "archive",          true },
    {FSEL_ROOT "/decks",         "japanese_n5.csv",  false},
    {FSEL_ROOT "/decks",         "gre_verbal.txt",   false},
    {FSEL_ROOT "/decks",         "cover.png",        false},
    {FSEL_ROOT "/decks/archive", "old_deck.csv",     false},
    {FSEL_ROOT "/notes",         "readme.md",        false},
};
#define MOCK_VFS_SIZE (sizeof(mock_vfs) / sizeof(mock_vfs[0]))

static uint16_t mock_list_dir(const char *path, fsel_entry_t *out, uint16_t max)
{
    uint16_t n = 0;
    for (size_t i = 0; i < MOCK_VFS_SIZE && n < max; i++) {
        if (strcmp(mock_vfs[i].parent, path) != 0) continue;
        strncpy(out[n].name, mock_vfs[i].name, FSEL_MAX_NAME - 1);
        out[n].name[FSEL_MAX_NAME - 1] = '\0';
        out[n].is_dir = mock_vfs[i].is_dir;
        n++;
    }
    return n;
}

bool folder_selector_using_mock(void)
{
    return !sd_control_is_ready(FSEL_ROOT);
}

/* 统一入口：有真实文件系统就读真实目录，否则回落到 mock 树 */
static uint16_t fsel_list_dir(const char *path, fsel_entry_t *out, uint16_t max)
{
    if (folder_selector_using_mock()) {
        return mock_list_dir(path, out, max);
    }

    file_list_t list = {0};
    if (!sd_control_get_dir_list(path, &list, NULL)) {
        ESP_LOGW(TAG, "read dir failed: %s", path);
        return 0;
    }

    uint16_t n = 0;
    for (uint32_t i = 0; i < list.count && n < max; i++) {
        strncpy(out[n].name, list.nodes[i].name, FSEL_MAX_NAME - 1);
        out[n].name[FSEL_MAX_NAME - 1] = '\0';
        out[n].is_dir = (list.nodes[i].type == NODE_TYPE_DIR);
        n++;
    }
    sd_control_free_dir_list(&list);
    return n;
}

/* ── 路径辅助 ───────────────────────────────────────────────────────── */

static void path_go_up(char *path)
{
    /* 不允许退到 FSEL_ROOT 以上 */
    if (strcmp(path, FSEL_ROOT) == 0) return;

    char *last_slash = strrchr(path, '/');
    if (!last_slash) return;
    if (last_slash == path) *(last_slash + 1) = '\0';
    else                    *last_slash = '\0';

    if (strlen(path) < strlen(FSEL_ROOT)) {
        strcpy(path, FSEL_ROOT);
    }
}

static void path_append(char *path, const char *sub)
{
    size_t len = strlen(path);
    if (len + strlen(sub) + 2 >= FS_MAX_PATH_LEN) return;
    if (len == 0 || path[len - 1] != '/') strcat(path, "/");
    strcat(path, sub);
}

/* filter_ext 形如 ".txt,.csv"；NULL 表示全部通过 */
static bool ext_accepted(const char *name, const char *filter_ext)
{
    if (!filter_ext || !filter_ext[0]) return true;

    const char *dot = strrchr(name, '.');
    if (!dot) return false;

    const char *p = filter_ext;
    while (*p) {
        while (*p == ',' || *p == ' ') p++;
        const char *end = p;
        while (*end && *end != ',') end++;
        size_t len = (size_t)(end - p);
        if (len > 0 && strlen(dot) == len && strncasecmp(dot, p, len) == 0) return true;
        p = end;
    }
    return false;
}

/* ── 选中状态 ───────────────────────────────────────────────────────── */

static void toggle_selection(fs_selector_ctx_t *ctx, const char *full_path, bool selected)
{
    if (selected) {
        if (ctx->selected_count < FS_MAX_SELECTIONS) {
            strncpy(ctx->selected_paths[ctx->selected_count], full_path, FS_MAX_PATH_LEN - 1);
            ctx->selected_paths[ctx->selected_count][FS_MAX_PATH_LEN - 1] = '\0';
            ctx->selected_count++;
        }
        return;
    }
    for (int i = 0; i < ctx->selected_count; i++) {
        if (strcmp(ctx->selected_paths[i], full_path) != 0) continue;
        for (int j = i; j < ctx->selected_count - 1; j++) {
            strcpy(ctx->selected_paths[j], ctx->selected_paths[j + 1]);
        }
        ctx->selected_count--;
        break;
    }
}

/* 按钮的 user_data 里存的是 entries[] 下标 +1（0 保留给 ".."） */
static const fsel_entry_t *entry_of(fs_selector_ctx_t *ctx, lv_obj_t *btn)
{
    intptr_t tag = (intptr_t)lv_obj_get_user_data(btn);
    if (tag <= 0 || tag > ctx->entry_count) return NULL;
    return &ctx->entries[tag - 1];
}

static void update_hint(fs_selector_ctx_t *ctx)
{
    if (!ctx->hint_label) return;
    if (ctx->mode == FS_SEL_MODE_SINGLE_DIR) {
        lv_label_set_text(ctx->hint_label, "OK selects this folder");
    } else if (ctx->selected_count == 0) {
        lv_label_set_text(ctx->hint_label, "Nothing selected");
    } else if (ctx->selected_count == 1) {
        lv_label_set_text_fmt(ctx->hint_label, "Selected: %s", ctx->selected_paths[0]);
    } else {
        lv_label_set_text_fmt(ctx->hint_label, "Selected: %u items", ctx->selected_count);
    }
}

/* ── 事件 ───────────────────────────────────────────────────────────── */

static void checkbox_event_cb(lv_event_t *e)
{
    lv_obj_t *cb = lv_event_get_target(e);
    fs_selector_ctx_t *ctx = lv_event_get_user_data(e);
    if (!ctx) return;

    lv_obj_t *row = lv_obj_get_parent(cb);
    lv_obj_t *btn = lv_obj_get_child(row, 0);
    const fsel_entry_t *entry = entry_of(ctx, btn);
    if (!entry) return;

    char full_path[FS_MAX_PATH_LEN];
    strncpy(full_path, ctx->current_path, sizeof(full_path) - 1);
    full_path[sizeof(full_path) - 1] = '\0';
    path_append(full_path, entry->name);

    toggle_selection(ctx, full_path, lv_obj_has_state(cb, LV_STATE_CHECKED));
    update_hint(ctx);
}

static void list_btn_event_cb(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_target(e);
    fs_selector_ctx_t *ctx = lv_event_get_user_data(e);
    if (!ctx) return;

    const fsel_entry_t *entry = entry_of(ctx, btn);

    /* user_data 为 0 → ".." 返回上一级 */
    if (!entry) {
        path_go_up(ctx->current_path);
        ctx->selected_count = 0;   /* 离开目录时清空已选 */
        refresh_path_list(ctx);
        return;
    }

    if (entry->is_dir) {
        path_append(ctx->current_path, entry->name);
        ctx->selected_count = 0;
        refresh_path_list(ctx);
        return;
    }

    if (ctx->mode == FS_SEL_MODE_SINGLE_FILE) {
        char full_path[FS_MAX_PATH_LEN];
        strncpy(full_path, ctx->current_path, sizeof(full_path) - 1);
        full_path[sizeof(full_path) - 1] = '\0';
        path_append(full_path, entry->name);

        ctx->selected_count = 0;
        toggle_selection(ctx, full_path, true);

        /* 视觉反馈：清掉兄弟行的选中态，只高亮当前行 */
        lv_obj_t *list = lv_obj_get_parent(lv_obj_get_parent(btn));
        for (uint32_t i = 0; i < lv_obj_get_child_count(list); i++) {
            lv_obj_t *sibling = lv_obj_get_child(lv_obj_get_child(list, i), 0);
            if (sibling) lv_obj_remove_state(sibling, LV_STATE_CHECKED);
        }
        lv_obj_add_state(btn, LV_STATE_CHECKED);
        update_hint(ctx);
    }
}

static void confirm_event_handler(lv_event_t *e)
{
    fs_selector_ctx_t *ctx = lv_event_get_user_data(e);
    if (!ctx || !ctx->callback) return;

    memset(&ctx->final_selection, 0, sizeof(fs_selection_t));

    if (ctx->mode == FS_SEL_MODE_SINGLE_DIR) {
        /* 单选目录直接返回当前所在路径 */
        ctx->path_ptrs[0] = ctx->current_path;
        ctx->final_selection.paths = ctx->path_ptrs;
        ctx->final_selection.count = 1;
    } else {
        if (ctx->selected_count == 0) {
            ESP_LOGW(TAG, "OK pressed with nothing selected");
            return;
        }
        for (int i = 0; i < ctx->selected_count; i++) {
            ctx->path_ptrs[i] = ctx->selected_paths[i];
        }
        ctx->final_selection.paths = ctx->path_ptrs;
        ctx->final_selection.count = ctx->selected_count;
    }

    ctx->callback(&ctx->final_selection, ctx->user_data);
}

static void cleanup_event_handler(lv_event_t *e)
{
    fs_selector_ctx_t *ctx = lv_event_get_user_data(e);
    free(ctx);
}

/* ── 列表构建 ───────────────────────────────────────────────────────── */

/* 一行 = 容器 + 左侧图标文字按钮（+ 多选模式下右侧 checkbox） */
static lv_obj_t *create_list_row(lv_obj_t *parent, const char *icon, const char *name)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_size(row, LV_PCT(100), 50);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);

    lv_obj_t *btn = lv_btn_create(row);
    lv_obj_set_flex_grow(btn, 1);
    lv_obj_set_height(btn, LV_PCT(100));

    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text_fmt(lbl, "%s  %s", icon, name);
    lv_obj_set_width(lbl, LV_PCT(100));
    /* 墨水屏上不要用 SCROLL_CIRCULAR：那是永不停止的动画，会一直触发局部刷新 */
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_center(lbl);

    return row;
}

static void refresh_path_list(fs_selector_ctx_t *ctx)
{
    lv_obj_clean(ctx->path_list);
    lv_label_set_text(ctx->path_label, ctx->current_path);

    ctx->entry_count = fsel_list_dir(ctx->current_path, ctx->entries, FSEL_MAX_ENTRIES);

    /* 1. ".." 返回上一级（根目录不显示） */
    if (strcmp(ctx->current_path, FSEL_ROOT) != 0) {
        lv_obj_t *row = create_list_row(ctx->path_list, LV_SYMBOL_DIRECTORY, "..");
        lv_obj_t *btn = lv_obj_get_child(row, 0);
        lv_obj_set_user_data(btn, (void *)(intptr_t)0);
        lv_obj_add_event_cb(btn, list_btn_event_cb, LV_EVENT_CLICKED, ctx);
    }

    /* 2. 目录内容 */
    bool dir_mode = (ctx->mode == FS_SEL_MODE_SINGLE_DIR || ctx->mode == FS_SEL_MODE_MULTI_DIR);

    for (uint16_t i = 0; i < ctx->entry_count; i++) {
        const fsel_entry_t *entry = &ctx->entries[i];

        if (dir_mode && !entry->is_dir) continue;
        if (!entry->is_dir && !ext_accepted(entry->name, ctx->filter_ext)) continue;

        lv_obj_t *row = create_list_row(ctx->path_list,
                                        entry->is_dir ? LV_SYMBOL_DIRECTORY : LV_SYMBOL_FILE,
                                        entry->name);
        lv_obj_t *btn = lv_obj_get_child(row, 0);
        lv_obj_set_user_data(btn, (void *)(intptr_t)(i + 1));
        lv_obj_add_event_cb(btn, list_btn_event_cb, LV_EVENT_CLICKED, ctx);

        bool show_checkbox = (ctx->mode == FS_SEL_MODE_MULTI_DIR  &&  entry->is_dir) ||
                             (ctx->mode == FS_SEL_MODE_MULTI_FILE && !entry->is_dir);
        if (show_checkbox) {
            lv_obj_t *cb = lv_checkbox_create(row);
            lv_checkbox_set_text(cb, "");
            lv_obj_add_event_cb(cb, checkbox_event_cb, LV_EVENT_VALUE_CHANGED, ctx);
        }
    }

    update_hint(ctx);
}

/* ── 入口 ───────────────────────────────────────────────────────────── */

lv_obj_t *folder_selector_create_filtered(fs_selector_mode_t mode, const char *start_path,
                                          const char *filter_ext, file_selector_cb_t callback,
                                          lv_event_cb_t back_cb, void *nav_data)
{
    const char *title = (mode == FS_SEL_MODE_SINGLE_DIR || mode == FS_SEL_MODE_MULTI_DIR)
                        ? "Select Folder" : "Select File";
    Page page = lv_page_create(title, true, back_cb, nav_data);

    fs_selector_ctx_t *ctx = malloc(sizeof(fs_selector_ctx_t));
    if (!ctx) {
        ESP_LOGE(TAG, "Failed to allocate selector context");
        return NULL;
    }
    memset(ctx, 0, sizeof(fs_selector_ctx_t));

    ctx->mode       = mode;
    ctx->filter_ext = filter_ext;
    ctx->callback   = callback;
    ctx->user_data  = nav_data;
    strncpy(ctx->current_path, start_path ? start_path : FSEL_ROOT, FS_MAX_PATH_LEN - 1);

    ESP_LOGI(TAG, "open selector: mode=%d path=%s filter=%s source=%s",
             (int)mode, ctx->current_path, filter_ext ? filter_ext : "(none)",
             folder_selector_using_mock() ? "MOCK" : "real fs");

    ctx->path_label = lv_label_create(page.container);
    lv_obj_set_width(ctx->path_label, LV_PCT(100));
    lv_label_set_long_mode(ctx->path_label, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_style_pad_bottom(ctx->path_label, 6, 0);

    ctx->hint_label = lv_label_create(page.container);
    lv_obj_set_width(ctx->hint_label, LV_PCT(100));
    lv_label_set_long_mode(ctx->hint_label, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_style_text_font(ctx->hint_label, LV_FONT_SMALL, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(ctx->hint_label, 6, 0);

    ctx->path_list = lv_list_create(page.container);
    lv_obj_set_width(ctx->path_list, LV_PCT(100));
    lv_obj_set_flex_grow(ctx->path_list, 1);

    lv_obj_t *confirm_btn = lv_btn_create(page.container);
    lv_obj_set_width(confirm_btn, LV_PCT(100));
    lv_obj_set_height(confirm_btn, 60);
    lv_obj_set_style_margin_top(confirm_btn, 10, 0);
    lv_obj_t *btn_label = lv_label_create(confirm_btn);
    lv_label_set_text(btn_label, "OK");
    lv_obj_center(btn_label);

    lv_obj_add_event_cb(confirm_btn, confirm_event_handler, LV_EVENT_CLICKED, ctx);
    lv_obj_add_event_cb(page.screen, cleanup_event_handler, LV_EVENT_DELETE, ctx);

    refresh_path_list(ctx);

    return page.screen;
}

lv_obj_t *folder_selector_create(fs_selector_mode_t mode, const char *start_path,
                                 file_selector_cb_t callback, lv_event_cb_t back_cb,
                                 void *nav_data)
{
    return folder_selector_create_filtered(mode, start_path, NULL, callback, back_cb, nav_data);
}
