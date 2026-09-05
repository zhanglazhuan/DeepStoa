#include <lvgl.h>
#include "lv_ui_style_guide.h"

#include "view_import.h"
#include "../view.h"

// 点击按钮触发弹窗
static void import_from_sd_cb(lv_event_t *e) { //
    AnkiApp *app = lv_event_get_user_data(e); //
    PAGE_NAVIGATE_TO(app, PAGE_FILE_SELECTOR, NULL);
}

// 构建导入页面
static lv_obj_t* build_import_page(AnkiApp* app, void* user_data) {
    (void)user_data;
    Page page = lv_page_create("Import", true, page_navigator_navigate_back, &app->view->page_nav);

    // 导入方式选择
    lv_obj_t *import_cont = page.container;

    // 从 SD 卡导入按钮
    lv_obj_t *sd_btn = lv_btn_create(import_cont);
    ui_style_set_btn_primary(sd_btn);
    lv_obj_set_size(sd_btn, LV_PCT(98), 60);
    lv_obj_add_event_cb(sd_btn, import_from_sd_cb, LV_EVENT_CLICKED, app);
    
    lv_obj_t *sd_btn_label = lv_label_create(sd_btn);
    lv_label_set_text(sd_btn_label, "Text file (.txt, .csv)");
    lv_obj_center(sd_btn_label);

    return page.screen;
}

void anki_view_import_init_registry(struct AnkiApp* app) {
    PAGE_REGISTE(app, PAGE_IMPORT, build_import_page);
}