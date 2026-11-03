#include "view.h"

#include <stdlib.h>
#include "app.h"
#include "pages/view_main.h"

bool weather_view_init(WeatherApp *app)
{
    if (!app) return false;
    app->view = calloc(1, sizeof(WeatherView));
    if (!app->view) return false;
    page_navigator_page_t *registry = calloc(1, sizeof(*registry));
    if (!registry) {
        free(app->view);
        app->view = NULL;
        return false;
    }
    page_navigator_init(&app->view->page_nav, registry, 1, app);
    weather_view_main_init_registry(app);
    return true;
}

void weather_view_deinit(WeatherApp *app)
{
    if (!app || !app->view) return;
    free(app->view->page_nav.registry);
    page_navigator_deinit(&app->view->page_nav);
    free(app->view);
    app->view = NULL;
}
