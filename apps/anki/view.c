#include "esp_system.h"
#include <string.h>
#include "view.h"
#include "app.h"

#include "./subpages/view_decks.h"
#include "./subpages/view_study.h"
#include "./subpages/view_settings.h"
#include "./subpages/view_stats.h"
#include "./subpages/view_import.h"
#include "./subpages/view_file_selector.h"
#include "./subpages/view_deck_form.h"

void anki_view_init(AnkiApp *app) {
    app->view = malloc(sizeof(AnkiView));
    memset(app->view, 0, sizeof(AnkiView));

    page_navigator_page_t *registry = malloc(sizeof(page_navigator_page_t) * ANKI_PAGE_ID_MAX);
    memset(registry, 0, sizeof(page_navigator_page_t) * ANKI_PAGE_ID_MAX);
    page_navigator_init(&app->view->page_nav, registry, ANKI_PAGE_ID_MAX, app);

    anki_view_decks_init_registry(app);
    anki_view_deck_form_init_registry(app);
    anki_view_study_init_registry(app);
    anki_view_settings_init_registry(app);
    anki_view_stats_init_registry(app);
    anki_view_import_init_registry(app);
    anki_view_file_selector_init_registry(app);
}

void anki_view_deinit(AnkiApp *app) {
    if (app->view) {
        page_navigator_deinit(&app->view->page_nav);
        free(app->view->page_nav.registry);
        free(app->view);
        app->view = NULL;
    }
}
