#include <stdio.h>
#include "esp_system.h"
#include "esp_log.h"

#include "view.h"
#include "model.h"
#include "controller.h"
#include "./subpages/view_library.h"
#include "./subpages/view_reading.h"

static const char *TAG = "reader_view";

// --- 视图接口实现 ---
void reader_view_init(ReaderApp *app) {
    app->view = malloc(sizeof(ReaderView));
    memset(app->view, 0, sizeof(ReaderView));

    page_navigator_page_t *registry = malloc(sizeof(page_navigator_page_t) * READER_PAGE_ID_MAX);
    memset(registry, 0, sizeof(page_navigator_page_t) * READER_PAGE_ID_MAX);
    page_navigator_init(&app->view->page_nav, registry, READER_PAGE_ID_MAX, app);

    reader_view_library_init_registry(app);
    reader_view_reading_init_registry(app);
}

void reader_view_deinit(ReaderApp *app) {
    if (app->view) {
        page_navigator_deinit(&app->view->page_nav);
        free(app->view->page_nav.registry);
        free(app->view);
        app->view = NULL;
    }
}
