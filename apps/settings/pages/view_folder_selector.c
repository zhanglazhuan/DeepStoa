// view_folder_selector: one navigable page.
#include "lv_folder_selector.h"
#include "page_navigator.h"
#include "settings_app.h"
#include "settings_model.h"
#include "settings_view.h"
#include "settings_controller.h"
#include "view_folder_selector.h"

static void on_folder_selected(const fs_selection_t *selection, void *user_data) {
    page_navigator_t *nav = (page_navigator_t *)user_data;
    if (!nav) return;

    if (selection && selection->count > 0) {
        SettingsApp *app = (SettingsApp *)nav->app_handler;
        settings_controller_set_wallpaper_folder(app, selection->paths[0]);
    }

    /* 回到 Wallpaper 页。那一页会重新构建，直接显示 model 里的新路径。 */
    page_navigator_navigate_pop(nav, nav->app_handler);
}

static lv_obj_t *build_folder_selector_page(struct SettingsApp *app) {
    /* 选目录不是选文件，所以是 SINGLE_DIR；目录不受后缀过滤影响，用不过滤的版本。
     * 已经设过就从那个目录开始，省得每次从根目录点起。 */
    const char *start = app->model->wallpaper.wallpaper_folder[0] != '\0'
                            ? app->model->wallpaper.wallpaper_folder
                            : FSEL_ROOT;

    return folder_selector_create(FS_SEL_MODE_SINGLE_DIR, start,
                                  on_folder_selected,
                                  page_navigator_navigate_back,
                                  &app->view->page_nav);
}

void settings_view_folder_selector_init_registry(struct SettingsApp *app)
{
    PAGE_REGISTE(app, PAGE_GENERAL_FOLDER_SELECTOR, build_folder_selector_page);
}
