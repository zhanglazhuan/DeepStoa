#include "view.h"

#include <stdlib.h>
#include "app.h"
#include "pages/view_list.h"
#include "pages/view_detail.h"

bool mistakebook_view_init(MistakebookApp *app)
{
    app->view = calloc(1, sizeof(MistakebookView));
    if (!app->view) return false;
    page_navigator_page_t *registry = calloc(2, sizeof(*registry));
    if (!registry) {
        free(app->view);
        app->view = NULL;
        return false;
    }
    page_navigator_init(&app->view->page_nav, registry, 2, app);
    mistakebook_view_list_init_registry(app);
    mistakebook_view_detail_init_registry(app);
    return true;
}

void mistakebook_view_deinit(MistakebookApp *app)
{
    if (!app || !app->view) return;
    free(app->view->page_nav.registry);
    page_navigator_deinit(&app->view->page_nav);
    free(app->view);
    app->view = NULL;
}
