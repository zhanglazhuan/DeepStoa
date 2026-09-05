/**
 * @file lv_folder_selector.h
 * @brief 文件/文件夹选择器页面
 *
 * Ported from D:\Codes\EPOS\epos\epos_lv\widgets\lv_folder_selector.c
 *
 * Changes from EPOS:
 *   - zephyr/fs/fs.h + k_malloc/k_free → ESP-IDF VFS (system/controller/sd_control.h) + libc
 *   - 目录数据源可切换：挂载了文件系统就读真实目录，没挂载就用内置 mock 树，
 *     两种情况 UI 完全一致，因此现在没有 SD 卡也能调试整条交互链路。
 *   - 条目名不再从 label 文本里反解析，改为存在 button 的 user_data 里（索引）
 */

#ifndef LV_FOLDER_SELECTOR_H
#define LV_FOLDER_SELECTOR_H

#include <stdbool.h>
#include <stdint.h>
#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 文件系统根目录。真实 SD 卡挂载点也用这个，切到真实数据时路径无需改动。 */
#define FSEL_ROOT           "/sdcard"

#define FS_MAX_PATH_LEN     256
#define FS_MAX_SELECTIONS   9    /* 限制最大多选数量，避免内存碎片 */

/* 选择器的工作模式 */
typedef enum {
    FS_SEL_MODE_SINGLE_FILE,
    FS_SEL_MODE_MULTI_FILE,
    FS_SEL_MODE_SINGLE_DIR,
    FS_SEL_MODE_MULTI_DIR
} fs_selector_mode_t;

/* 回调返回的数据结构 */
typedef struct {
    const char **paths;   /* 选中的路径数组指针 */
    uint16_t     count;   /* 选中的数量 */
} fs_selection_t;

/* 确认回调：接收结构体指针和用户上下文 */
typedef void (*file_selector_cb_t)(const fs_selection_t *selection, void *user_data);

/**
 * @brief 创建文件/文件夹选择器页面
 * @param mode        工作模式 (单选/多选、文件/目录)
 * @param start_path  初始路径，传 NULL 默认为 FSEL_ROOT
 * @param filter_ext  只显示这些后缀的文件，逗号分隔（如 ".txt,.csv"）；NULL 表示不过滤。
 *                    目录始终显示，不受过滤影响。
 * @param callback    点 OK 时的回调
 * @param back_cb     返回键回调（通常传 page_navigator_navigate_back）
 * @param nav_data    透传给 back_cb 和 callback 的上下文
 * @return 页面 screen 对象；失败返回 NULL
 */
lv_obj_t *folder_selector_create_filtered(fs_selector_mode_t mode, const char *start_path,
                                          const char *filter_ext, file_selector_cb_t callback,
                                          lv_event_cb_t back_cb, void *nav_data);

/** 不过滤后缀的简化版本（等价于 filter_ext = NULL） */
lv_obj_t *folder_selector_create(fs_selector_mode_t mode, const char *start_path,
                                 file_selector_cb_t callback, lv_event_cb_t back_cb,
                                 void *nav_data);

/** 当前列出的是内置 mock 数据（true）还是真实文件系统（false） */
bool folder_selector_using_mock(void);

#ifdef __cplusplus
}
#endif

#endif /* LV_FOLDER_SELECTOR_H */
