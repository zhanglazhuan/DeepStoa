#include <lvgl.h>
#include <string.h>
#include "controller_decks.h"
#include "../controller.h"
#include "../model.h"
#include "../view.h"
#include "../storage.h"


void anki_controller_on_deck_clicked(lv_event_t *e) {
    AnkiApp *app = lv_event_get_user_data(e);
    lv_obj_t *target = lv_event_get_target(e);
    int deck_idx = (int)(intptr_t)lv_obj_get_user_data(target);
    if (!app || !app->model || deck_idx < 0 ||
        deck_idx >= app->model->deck_count) return;

    app->model->active_deck_idx = deck_idx;
    app->model->active_card_idx = 0;
    app->model->is_showing_answer = false;
    anki_model_begin_study(app->model);
    
    if (!anki_storage_sync_meta(app->model)) return;

    int tree_cards = anki_model_get_tree_total_cards(app->model, deck_idx);
    if (tree_cards > 0) {
        PAGE_NAVIGATE_TO(app, PAGE_STUDY, NULL);
    }
}

// === 新增以下代码 ===
bool anki_controller_add_root_deck(AnkiApp *app, const char *name) {
    if (!app || !app->model) return false;
    uint32_t previous_generator = app->model->_id_generator;
    if (anki_model_add_deck(app->model, name, -1)) {
        if (anki_storage_sync_decks(app->model)) return true;
        app->model->deck_count--;
        app->model->_id_generator = previous_generator;
    }
    return false;
}

bool anki_controller_add_sub_deck(AnkiApp *app, int parent_idx, const char *name) {
    if (!app || !app->model) return false;
    uint32_t previous_generator = app->model->_id_generator;
    if (anki_model_add_deck(app->model, name, parent_idx)) {
        if (anki_storage_sync_decks(app->model)) return true;
        app->model->deck_count--;
        app->model->_id_generator = previous_generator;
    }
    return false;
}

bool anki_controller_edit_deck(AnkiApp *app, int deck_idx, const char *name) {
    if (!app || !app->model || deck_idx < 0 ||
        deck_idx >= app->model->deck_count) return false;
    char previous[sizeof(app->model->decks[deck_idx].name)];
    memcpy(previous, app->model->decks[deck_idx].name, sizeof(previous));
    if (anki_model_update_deck(app->model, deck_idx, name)) {
        if (anki_storage_sync_decks(app->model)) return true;
        memcpy(app->model->decks[deck_idx].name, previous,
               sizeof(previous));
    }
    return false;
}
