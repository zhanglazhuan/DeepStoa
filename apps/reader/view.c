#include <stdio.h>
#include "esp_system.h"
#include "esp_log.h"

#include "view.h"
#include "model.h"
#include "controller.h"
#include "./subpages/view_library.h"
#include "./subpages/view_reading.h"

// --- 视图接口实现 ---
void reader_view_init(ReaderApp *app) {
    app->view = malloc(sizeof(ReaderView));
    if (!app->view) return;
    memset(app->view, 0, sizeof(ReaderView));

    page_navigator_page_t *registry = malloc(sizeof(page_navigator_page_t) * READER_PAGE_ID_MAX);
    if (!registry) {
        free(app->view);
        app->view = NULL;
        return;
    }
    memset(registry, 0, sizeof(page_navigator_page_t) * READER_PAGE_ID_MAX);
    page_navigator_init(&app->view->page_nav, registry, READER_PAGE_ID_MAX, app);

    reader_view_library_init_registry(app);
    reader_view_reading_init_registry(app);
}

void reader_view_deinit(ReaderApp *app) {
    if (app->view) {
        reader_view_release_media(app);
        page_navigator_deinit(&app->view->page_nav);
        free(app->view->page_nav.registry);
        free(app->view);
        app->view = NULL;
    }
}

void reader_view_release_media(ReaderApp *app) {
    if (!app || !app->view) return;
    ReaderViewCtx *ctx = &app->view->ctx;
    if (ctx->toc_panel && lv_obj_is_valid(ctx->toc_panel)) lv_obj_delete(ctx->toc_panel);
    ctx->toc_panel = NULL;
    if (ctx->image_obj && lv_obj_is_valid(ctx->image_obj)) lv_obj_delete(ctx->image_obj);
    ctx->image_obj = NULL;
    if (ctx->image_data) reader_model_free_page_image(ctx->image_data);
    ctx->image_data = NULL;
}
