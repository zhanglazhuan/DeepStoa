#include <stdio.h>
#include <lvgl.h>
#include "esp_log.h"

#include "widgets/lv_folder_selector.h"
#include "widgets/lv_toast.h"
#include "view_file_selector.h"
#include "../view.h"

static const char *TAG = "anki_view_file_selector";

/* 只接受文本卡片文件，和 Import 页按钮上写的一致 */
#define IMPORT_FILTER_EXT   ".txt,.csv"

// 选中文件后的回调
static void on_file_selected(const fs_selection_t *selection, void *user_data) {
    page_navigator_t *nav = (page_navigator_t *)user_data;
    if (!selection || selection->count == 0) return;

    const char *path = selection->paths[0];
    ESP_LOGI(TAG, "selected file: %s (count=%u)", path, (unsigned)selection->count);

    /* TODO: 真正的导入还没做 —— 这里应该调 anki_controller_import_file(app, path)，
     * 把 .txt/.csv 解析成 deck/card 写进 model。目前只回显选中的路径，
     * 让整条 UI 交互链路可以先跑通。 */
    if (folder_selector_using_mock()) {
        lv_toast_show("Mock file selected (import not implemented)", 2000);
    } else {
        lv_toast_show(path, 2000);
    }

    if (nav) page_navigator_navigate_pop(nav, nav->app_handler);
}

// 构建文件选择器页面
static lv_obj_t* build_file_selector_page(AnkiApp* app, void* user_data) {
    (void)user_data;

    if (folder_selector_using_mock()) {
        ESP_LOGW(TAG, "No filesystem mounted at %s - listing built-in mock tree", FSEL_ROOT);
    }

    return folder_selector_create_filtered(FS_SEL_MODE_SINGLE_FILE, FSEL_ROOT,
                                           IMPORT_FILTER_EXT, on_file_selected,
                                           page_navigator_navigate_back,
                                           &app->view->page_nav);
}

void anki_view_file_selector_init_registry(struct AnkiApp* app) {
    PAGE_REGISTE(app, PAGE_FILE_SELECTOR, build_file_selector_page);
}
