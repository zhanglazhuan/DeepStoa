#include "esp_system.h"
#include "esp_log.h"
#include <lvgl.h>

#include "controller.h"
#include "model.h"
#include "view.h"

static const char *TAG = "reader_ctrl";

/* ── Lifecycle ─────────────────────────────────────────────────── */

void reader_controller_init(ReaderApp *app) {
    app->controller = malloc(sizeof(ReaderController));
}

void reader_controller_deinit(ReaderApp *app) {
    if (app->controller) {
        free(app->controller);
        app->controller = NULL;
    }
}

/* ── Library page callbacks ────────────────────────────────────── */

void reader_controller_on_book_clicked(lv_event_t *e) {
    ReaderApp *app = lv_event_get_user_data(e);
    lv_obj_t *target = lv_event_get_target(e);
    uint8_t book_idx = (uint8_t)(uintptr_t)lv_obj_get_user_data(target);

    if (reader_model_open_book(app->model, book_idx)) {
        /* 必须用宏：只有宏会压返回栈，Reading 页的返回键
         * (reader_controller_on_back_to_library → navigate_pop) 才 pop 得回来 */
        PAGE_NAVIGATE_TO(app, PAGE_READING, NULL);
    }
}

/* ── Reading page callbacks ────────────────────────────────────── */

void reader_controller_on_read_next_page(lv_event_t *e) {
    ReaderApp *app = lv_event_get_user_data(e);
    ESP_LOGI(TAG, "reader_controller_on_read_next_page");
    uint32_t bytes_rendered = reader_view_get_rendered_bytes(app->view);
    reader_model_next_page(app->model, bytes_rendered);
    reader_view_refresh_read_page(app);
}

void reader_controller_on_read_prev_page(lv_event_t *e) {
    ReaderApp *app = lv_event_get_user_data(e);
    ESP_LOGI(TAG, "reader_controller_on_read_prev_page");
    uint32_t bytes_rendered = reader_view_get_rendered_bytes(app->view);
    reader_model_prev_page(app->model, bytes_rendered);
    reader_view_refresh_read_page(app);
}

void reader_controller_on_read_slider_changed(lv_event_t *e) {
    ReaderApp *app = lv_event_get_user_data(e);
    lv_obj_t *slider = lv_event_get_target(e);
    uint8_t percent = lv_slider_get_value(slider);

    reader_model_set_offset_by_percent(app->model, percent);
    reader_view_refresh_read_page(app);
}

void reader_controller_on_font_toggle_clicked(lv_event_t *e) {
    ReaderApp *app = lv_event_get_user_data(e);
    reader_model_toggle_font_size(app->model);
    reader_view_refresh_read_page(app);
}

void reader_controller_on_back_to_library(lv_event_t *e) {
    ReaderApp *app = &g_reader_app;
    page_navigator_navigate_pop(&app->view->page_nav, app);
}
