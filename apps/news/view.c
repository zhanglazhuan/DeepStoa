#include <stdlib.h>
#include <string.h>
#include <lvgl.h>
#include "esp_log.h"

#include "app.h"
#include "view.h"
#include "model.h"
#include "subpages/view_list.h"
#include "subpages/view_detail.h"
#include "subpages/view_launch.h"

static const char *TAG = "news_view";

/* ── lifecycle ───────────────────────────────────────────────── */

void news_view_init(NewsApp *app)
{
    app->view = malloc(sizeof(NewsView));
    if (!app->view) {
        ESP_LOGE(TAG, "Failed to alloc NewsView");
        return;
    }
    memset(app->view, 0, sizeof(NewsView));

    page_navigator_page_t *registry = malloc(sizeof(page_navigator_page_t) * NEWS_PAGE_ID_MAX);
    if (!registry) {
        ESP_LOGE(TAG, "Failed to alloc page registry");
        free(app->view);
        app->view = NULL;
        return;
    }
    memset(registry, 0, sizeof(page_navigator_page_t) * NEWS_PAGE_ID_MAX);
    page_navigator_init(&app->view->page_nav, registry, NEWS_PAGE_ID_MAX, app);

    /* Register sub-page builders */
    news_view_launch_init_registry(app);
    news_view_list_init_registry(app);
    news_view_detail_init_registry(app);
}

void news_view_deinit(NewsApp *app)
{
    if (!app->view) return;

    page_navigator_page_t *registry = app->view->page_nav.registry;
    page_navigator_deinit(&app->view->page_nav);
    free(registry);
    free(app->view);
    app->view = NULL;
}

/* ── controller → view ───────────────────────────────────────────
 * 页面是随导航重建的：离开列表页时 list_ctx.scroll 指向的对象已经被
 * 异步删除，异步回调再摸它就是野指针。所以每次同步都先确认那一页确实
 * 在前台；不在前台就什么都不做，页面下次构建时会直接从 model 渲染。 */

void news_view_sync_list(NewsApp *app)
{
    if (!app || !app->view || !app->model) return;
    if (app->view->page_nav.current_page != PAGE_NEWS_LIST) return;
    news_view_list_render(app);
}

void news_view_sync_detail(NewsApp *app)
{
    if (!app || !app->view || !app->model) return;
    if (app->view->page_nav.current_page != PAGE_NEWS_DETAIL) return;
    news_view_detail_render(app);
}
