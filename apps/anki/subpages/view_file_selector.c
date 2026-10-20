#include <stdio.h>
#include <lvgl.h>
#include "esp_log.h"

#include "widgets/lv_folder_selector.h"
#include "widgets/lv_toast.h"
#include "view_file_selector.h"
#include "../controller.h"
#include "../source_sync.h"
#include "../view.h"

static const char *TAG = "anki_view_file_selector";

/* 只接受文本卡片文件，和 Import 页按钮上写的一致 */
#define IMPORT_FILTER_EXT   ".txt"

static const char *sync_error_message(AnkiSyncError error)
{
    switch (error) {
        case ANKI_SYNC_ERR_TOO_LARGE:
            return "File is larger than 2 MiB";
        case ANKI_SYNC_ERR_TOO_MANY_LINES:
            return "File exceeds 10,000 lines";
        case ANKI_SYNC_ERR_TOO_MANY_CARDS:
            return "Deck exceeds 999 cards";
        case ANKI_SYNC_ERR_FORMAT:
            return "No valid front|back cards were found";
        case ANKI_SYNC_ERR_MEMORY:
            return "Not enough memory to import";
        case ANKI_SYNC_ERR_STORAGE:
            return "Card database is unavailable";
        case ANKI_SYNC_ERR_ID_EXHAUSTED:
            return "Card ID space is exhausted";
        default:
            return "Unable to read card file";
    }
}

// 选中文件后的回调
static void on_file_selected(const fs_selection_t *selection, void *user_data) {
    page_navigator_t *nav = (page_navigator_t *)user_data;
    if (!selection || selection->count == 0) return;

    const char *path = selection->paths[0];
    ESP_LOGI(TAG, "selected file: %s (count=%u)", path, (unsigned)selection->count);

    AnkiApp *app = nav ? (AnkiApp *)nav->app_handler : NULL;
    AnkiSyncResult result = {.error = ANKI_SYNC_ERR_ARGUMENT};
    if (!app || !anki_source_sync_file(app->model, path, &result)) {
        lv_toast_show(sync_error_message(result.error), 3000);
        return;
    }

    anki_controller_sync(app);
    char message[96];
    snprintf(message, sizeof(message),
             "Synced: %u new, %u updated, %u missing, %u removed, %u skipped",
             result.added, result.updated, result.missing, result.purged,
             result.skipped);
    lv_toast_show(message, 2500);

    page_navigator_navigate_pop(nav, nav->app_handler);
}

// 构建文件选择器页面
static lv_obj_t* build_file_selector_page(AnkiApp* app, void* user_data) {
    (void)user_data;

    return folder_selector_create_filtered(FS_SEL_MODE_SINGLE_FILE, FSEL_ROOT,
                                           IMPORT_FILTER_EXT, on_file_selected,
                                           page_navigator_navigate_back,
                                           &app->view->page_nav);
}

void anki_view_file_selector_init_registry(struct AnkiApp* app) {
    PAGE_REGISTE(app, PAGE_FILE_SELECTOR, build_file_selector_page);
}
