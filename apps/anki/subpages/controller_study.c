#include <stdio.h>
#include <time.h>

#include <lvgl.h>

#include "controller_study.h"
#include "controller_settings.h"
#include "view_study.h"
#include "../controller.h"
#include "../model.h"
#include "../scheduler.h"
#include "../storage.h"
#include "../view.h"
#include "time_service.h"

#include "esp_log.h"

static const char *TAG = "anki_study";
static uint32_t scheduling_time(const CardIndex *card)
{
    (void)card;
    uint32_t now = 0;
    return time_service_now(&now) ? now : 0;
}

static bool preview_current(AnkiApp *app, AnkiScheduleOutcome outcomes[4])
{
    if (!app || !app->model || app->model->active_card_idx < 0 ||
        app->model->active_card_idx >= app->model->queue_count) return false;
    CardIndex *card = &app->model->today_queue[app->model->active_card_idx];
    AnkiSettings *settings =
        anki_controller_settings_get_for_deck(app, card->deck_id);
    if (!settings) return false;
    uint32_t now = scheduling_time(card);
    if (!now) return false;
    AnkiScheduleState schedule;
    anki_storage_load_schedule(card, &schedule);
    if (schedule.last_review_at > now) now = schedule.last_review_at;
    return anki_scheduler_preview(
        &schedule, card->status, card->card_id, now,
        settings->desired_retention_percent,
        settings->learning_step_minutes,
        settings->relearning_step_minutes, outcomes);
}

void anki_controller_on_show_answer(lv_event_t *e)
{
    AnkiApp *app = lv_event_get_user_data(e);
    if (!app || !app->model || !app->model->current_card_available) return;
    app->model->is_showing_answer = true;
    anki_view_study_card_flip(app);
}

void anki_controller_study_format_labels(AnkiApp *app,
                                         char labels[4][24])
{
    static const char *names[4] = {"Again", "Hard", "Good", "Easy"};
    AnkiScheduleOutcome outcomes[4];
    if (!preview_current(app, outcomes)) {
        for (int i = 0; i < 4; i++) snprintf(labels[i], 24, "%s", names[i]);
        return;
    }
    CardIndex *card = &app->model->today_queue[app->model->active_card_idx];
    uint32_t now = scheduling_time(card);
    for (int i = 0; i < 4; i++) {
        uint32_t delay = outcomes[i].due_at > now ? outcomes[i].due_at - now : 0;
        if (delay < 86400U) {
            uint32_t minutes = (delay + 59U) / 60U;
            snprintf(labels[i], 24, "%s\n%um", names[i], (unsigned)minutes);
        } else if (outcomes[i].interval_days < 730U) {
            snprintf(labels[i], 24, "%s\n%ud", names[i],
                     (unsigned)outcomes[i].interval_days);
        } else {
            snprintf(labels[i], 24, "%s\n%.1fy", names[i],
                     outcomes[i].interval_days / 365.0);
        }
    }
}

void anki_controller_on_rate_card(lv_event_t *e)
{
    AnkiApp *app = lv_event_get_user_data(e);
    if (!app || !app->model || !app->model->current_card_available ||
        !app->model->is_showing_answer || app->model->rating_in_progress ||
        app->model->active_card_idx < 0 ||
        app->model->active_card_idx >= app->model->queue_count) return;

    int rating = (int)(intptr_t)lv_obj_get_user_data(
        lv_event_get_current_target(e));
    if (rating < ANKI_RATING_AGAIN || rating > ANKI_RATING_EASY) return;
    app->model->rating_in_progress = true;

    CardIndex *card = &app->model->today_queue[app->model->active_card_idx];
    uint8_t previous_status = card->status & ANKI_CARD_STATUS_MASK;
    AnkiScheduleState previous_schedule;
    bool had_schedule = anki_storage_load_schedule(card, &previous_schedule);
    AnkiScheduleOutcome outcomes[4];
    if (!preview_current(app, outcomes)) {
        app->model->rating_in_progress = false;
        return;
    }
    AnkiScheduleOutcome *selected = &outcomes[rating - 1];

    CardIndex previous_card = *card;
    card->status = selected->card_status;
    card->due_timestamp = selected->due_at;
    card->interval = selected->interval_days > UINT8_MAX
        ? UINT8_MAX : (uint8_t)selected->interval_days;
    card->ease_factor = (uint8_t)(selected->schedule.difficulty * 10.0f);

    if (!anki_storage_save_schedule(&selected->schedule) ||
        !anki_model_update_card(app->model, card)) {
        *card = previous_card;
        if (had_schedule) anki_storage_save_schedule(&previous_schedule);
        else anki_storage_delete_schedule(card->card_id);
        ESP_LOGE(TAG, "Failed to persist rating for card %u",
                 (unsigned)card->card_id);
        app->model->rating_in_progress = false;
        return;
    }
    time_service_checkpoint(selected->schedule.last_review_at);

    uint32_t reviewed_at = selected->schedule.last_review_at;
    uint32_t elapsed_seconds = previous_schedule.last_review_at &&
        reviewed_at > previous_schedule.last_review_at
        ? reviewed_at - previous_schedule.last_review_at : 0;
    uint32_t elapsed_days = elapsed_seconds / 86400U;
    if (!anki_storage_append_review(
            card->card_id, reviewed_at,
            elapsed_days > UINT16_MAX ? UINT16_MAX : (uint16_t)elapsed_days,
            selected->interval_days > UINT16_MAX
                ? UINT16_MAX : (uint16_t)selected->interval_days,
            (uint8_t)rating, previous_status, selected->card_status)) {
        ESP_LOGW(TAG, "Review history not saved for card %u",
                 (unsigned)card->card_id);
    }

    anki_model_record_answer(app->model, card->deck_id,
        previous_status == ANKI_CARD_STATUS_NEW,
        previous_status == ANKI_CARD_STATUS_REVIEW);
    app->model->active_card_idx++;
    anki_storage_sync_meta(app->model);
    anki_controller_sync(app);

    if (!anki_model_load_next_card(app->model)) {
        anki_view_study_show_finished_msgbox(app);
    } else {
        app->model->is_showing_answer = false;
        anki_view_study_refresh_card(app);
    }
}
