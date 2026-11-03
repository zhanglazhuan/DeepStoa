#include "esp_system.h"
#include <string.h>
#include "view.h"
#include "app.h"

#include "./pages/view_decks.h"
#include "./pages/view_study.h"
#include "./pages/view_settings.h"
#include "./pages/view_stats.h"
#include "./pages/view_import.h"
#include "./pages/view_file_selector.h"
#include "./pages/view_trash.h"
#include "./pages/view_deck_form.h"

bool anki_view_init(AnkiApp *app) {
    if (!app) return false;
    app->view = malloc(sizeof(AnkiView));
    if (!app->view) return false;
    memset(app->view, 0, sizeof(AnkiView));

    page_navigator_page_t *registry = malloc(sizeof(page_navigator_page_t) * ANKI_PAGE_ID_MAX);
    if (!registry) {
        free(app->view);
        app->view = NULL;
        return false;
    }
    memset(registry, 0, sizeof(page_navigator_page_t) * ANKI_PAGE_ID_MAX);
    page_navigator_init(&app->view->page_nav, registry, ANKI_PAGE_ID_MAX, app);

    anki_view_decks_init_registry(app);
    anki_view_deck_form_init_registry(app);
    anki_view_study_init_registry(app);
    anki_view_settings_init_registry(app);
    anki_view_stats_init_registry(app);
    anki_view_import_init_registry(app);
    anki_view_file_selector_init_registry(app);
    anki_view_trash_init_registry(app);
    return true;
}

void anki_view_deinit(AnkiApp *app) {
    if (app->view) {
        page_navigator_deinit(&app->view->page_nav);
        free(app->view->page_nav.registry);
        free(app->view);
        app->view = NULL;
    }
}
