#include <lvgl.h>
#include "controller_decks.h"
#include "../controller.h"
#include "../model.h"
#include "../view.h"
#include "../storage.h"


void anki_controller_on_deck_clicked(lv_event_t *e) {
    AnkiApp *app = lv_event_get_user_data(e);
    lv_obj_t *target = lv_event_get_target(e);
    int deck_idx = (int)(intptr_t)lv_obj_get_user_data(target);

    app->model->active_deck_idx = deck_idx;
    app->model->active_card_idx = 0;
    app->model->is_showing_answer = false;
    
    anki_storage_sync_meta(app->model);

    int tree_cards = anki_model_get_tree_total_cards(app->model, deck_idx);
    if (tree_cards > 0) {
        PAGE_NAVIGATE_TO(app, PAGE_STUDY, NULL);
    }
}

// === 新增以下代码 ===
void anki_controller_add_root_deck(AnkiApp *app, const char *name) {
    if (anki_model_add_deck(app->model, name, -1)) {
        anki_storage_sync_decks(app->model);
    }
}

void anki_controller_add_sub_deck(AnkiApp *app, int parent_idx, const char *name) {
    if (anki_model_add_deck(app->model, name, parent_idx)) {
        anki_storage_sync_decks(app->model);
    }
}

void anki_controller_edit_deck(AnkiApp *app, int deck_idx, const char *name) {
    if (anki_model_update_deck(app->model, deck_idx, name)) {
        anki_storage_sync_decks(app->model);
    }
}
