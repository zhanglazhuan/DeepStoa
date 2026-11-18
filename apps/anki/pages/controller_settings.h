#ifndef ANKI_CONTROLLER_SETTINGS_H
#define ANKI_CONTROLLER_SETTINGS_H

#include <lvgl.h>
#include "../app.h"
#include "model.h"

// 每日计划卡片数变更
void anki_controller_settings_new_cards_changed(lv_event_t *e);
void anki_controller_settings_max_cards_changed(lv_event_t *e);
// 学习时长变更
void anki_controller_settings_study_duration_changed(lv_event_t *e);
// 允许提前学习开关变更
void anki_controller_settings_advance_study_changed(lv_event_t *e);

void anki_controller_settings_retention_changed(lv_event_t *e);
void anki_controller_settings_learning_step_changed(lv_event_t *e);
void anki_controller_settings_relearning_step_changed(lv_event_t *e);

AnkiSettings* anki_controller_settings_get_config(AnkiApp *app);
AnkiSettings* anki_controller_settings_get_for_deck(AnkiApp *app,
                                                     uint32_t deck_id);

#endif // ANKI_CONTROLLER_SETTINGS_H
