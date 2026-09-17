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

// 复习间隔变更
void anki_controller_settings_hard_interval_changed(lv_event_t *e);
void anki_controller_settings_good_interval_changed(lv_event_t *e);
void anki_controller_settings_easy_interval_changed(lv_event_t *e);

AnkiSettings* anki_controller_settings_get_config(AnkiApp *app);

#endif // ANKI_CONTROLLER_SETTINGS_H
