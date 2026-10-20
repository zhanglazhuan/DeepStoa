#include <math.h>
#include <stddef.h>
#include <string.h>

#include "model.h"
#include "scheduler.h"

static const float s_weights[21] = {
    0.212f, 1.2931f, 2.3065f, 8.2956f, 6.4133f, 0.8334f, 3.0194f,
    0.001f, 1.8722f, 0.1666f, 0.796f, 1.4835f, 0.0614f, 0.2629f,
    1.6483f, 0.6014f, 1.8729f, 0.5425f, 0.0912f, 0.0658f, 0.1542f,
};

static float clampf(float value, float minimum, float maximum)
{
    return fminf(fmaxf(value, minimum), maximum);
}

static float initial_stability(AnkiRating rating)
{
    return clampf(s_weights[rating - 1], 0.1f,
                  (float)ANKI_FSRS_MAX_INTERVAL_DAYS);
}

static float initial_difficulty_raw(AnkiRating rating)
{
    return s_weights[4] - expf(s_weights[5] * (rating - 1)) + 1.0f;
}

static float initial_difficulty(AnkiRating rating)
{
    return clampf(initial_difficulty_raw(rating), 1.0f, 10.0f);
}

static float next_difficulty(float difficulty, AnkiRating rating)
{
    float delta = -s_weights[6] * ((int)rating - 3);
    float damped = (10.0f - difficulty) * delta / 9.0f;
    float next = difficulty + damped;
    float easy_mean = initial_difficulty_raw(ANKI_RATING_EASY);
    next = s_weights[7] * easy_mean + (1.0f - s_weights[7]) * next;
    return clampf(next, 1.0f, 10.0f);
}

static float forgetting_curve(float elapsed_days, float stability)
{
    float decay = -s_weights[20];
    float factor = powf(0.9f, 1.0f / decay) - 1.0f;
    stability = clampf(stability, 0.001f,
                       (float)ANKI_FSRS_MAX_INTERVAL_DAYS);
    return powf(1.0f + factor * elapsed_days / stability, decay);
}

static float short_term_stability(float stability, AnkiRating rating)
{
    stability = clampf(stability, 0.001f,
                       (float)ANKI_FSRS_MAX_INTERVAL_DAYS);
    float increase = expf(s_weights[17] *
                          (((int)rating - 3) + s_weights[18])) *
                     powf(stability, -s_weights[19]);
    if (rating >= ANKI_RATING_HARD && increase < 1.0f) increase = 1.0f;
    return clampf(stability * increase, 0.001f,
                  (float)ANKI_FSRS_MAX_INTERVAL_DAYS);
}

static float next_recall_stability(float difficulty, float stability,
                                   float retrievability, AnkiRating rating)
{
    float hard_penalty = rating == ANKI_RATING_HARD ? s_weights[15] : 1.0f;
    float easy_bonus = rating == ANKI_RATING_EASY ? s_weights[16] : 1.0f;
    float next = stability *
        (1.0f + expf(s_weights[8]) * (11.0f - difficulty) *
         powf(stability, -s_weights[9]) *
         (expf((1.0f - retrievability) * s_weights[10]) - 1.0f) *
         hard_penalty * easy_bonus);
    return clampf(next, 0.001f, (float)ANKI_FSRS_MAX_INTERVAL_DAYS);
}

static float next_forget_stability(float difficulty, float stability,
                                   float retrievability)
{
    float next = s_weights[11] * powf(difficulty, -s_weights[12]) *
                 (powf(stability + 1.0f, s_weights[13]) - 1.0f) *
                 expf((1.0f - retrievability) * s_weights[14]);
    float ceiling = stability / expf(s_weights[17] * s_weights[18]);
    return clampf(fminf(next, ceiling), 0.001f,
                  (float)ANKI_FSRS_MAX_INTERVAL_DAYS);
}

static uint32_t mix32(uint32_t value)
{
    value ^= value >> 16;
    value *= UINT32_C(0x7feb352d);
    value ^= value >> 15;
    value *= UINT32_C(0x846ca68b);
    return value ^ (value >> 16);
}

static uint32_t fuzz_interval(uint32_t interval, uint32_t card_id,
                              uint16_t reps, AnkiRating rating)
{
    if (interval < 3) return interval;
    uint32_t minimum = interval * 95U / 100U;
    if (minimum > 1) minimum--;
    if (minimum < 2) minimum = 2;
    uint32_t maximum = interval * 105U / 100U + 1U;
    if (maximum > ANKI_FSRS_MAX_INTERVAL_DAYS) {
        maximum = ANKI_FSRS_MAX_INTERVAL_DAYS;
    }
    if (minimum >= maximum) return minimum;
    uint32_t seed = mix32(card_id ^ ((uint32_t)reps << 8) ^ (uint32_t)rating);
    return minimum + seed % (maximum - minimum + 1U);
}

static uint32_t interval_for_stability(float stability, int retention_percent,
                                       uint32_t card_id, uint16_t reps,
                                       AnkiRating rating)
{
    float retention = clampf(retention_percent / 100.0f, 0.70f, 0.97f);
    float decay = -s_weights[20];
    float factor = powf(0.9f, 1.0f / decay) - 1.0f;
    float raw = stability / factor * (powf(retention, 1.0f / decay) - 1.0f);
    uint32_t interval = (uint32_t)clampf(roundf(raw), 1.0f,
                                        (float)ANKI_FSRS_MAX_INTERVAL_DAYS);
    return fuzz_interval(interval, card_id, reps, rating);
}

static uint32_t add_seconds(uint32_t now, uint64_t seconds)
{
    uint64_t due = (uint64_t)now + seconds;
    return due > UINT32_MAX ? UINT32_MAX : (uint32_t)due;
}

float anki_scheduler_retrievability(const AnkiScheduleState *state,
                                    uint32_t now)
{
    if (!state || state->stability <= 0.0f || state->last_review_at == 0) {
        return 0.0f;
    }
    float elapsed = now > state->last_review_at
        ? (now - state->last_review_at) / 86400.0f : 0.0f;
    return forgetting_curve(elapsed, state->stability);
}

static void compute_memory(const AnkiScheduleState *current, AnkiRating rating,
                           uint32_t now, AnkiScheduleState *next)
{
    *next = *current;
    bool initialized = current->stability > 0.0f &&
                       current->difficulty >= 1.0f &&
                       current->difficulty <= 10.0f;
    if (!initialized) {
        next->stability = initial_stability(rating);
        next->difficulty = initial_difficulty(rating);
    } else {
        float elapsed = now > current->last_review_at
            ? (now - current->last_review_at) / 86400.0f : 0.0f;
        next->difficulty = next_difficulty(current->difficulty, rating);
        if (elapsed < 1.0f) {
            next->stability = short_term_stability(current->stability, rating);
        } else {
            float retrievability = forgetting_curve(elapsed, current->stability);
            next->stability = rating == ANKI_RATING_AGAIN
                ? next_forget_stability(current->difficulty, current->stability,
                                        retrievability)
                : next_recall_stability(current->difficulty, current->stability,
                                        retrievability, rating);
        }
    }
    next->last_review_at = now;
    if (next->reps < UINT16_MAX) next->reps++;
}

bool anki_scheduler_preview(const AnkiScheduleState *current,
                            uint8_t card_status,
                            uint32_t card_id,
                            uint32_t now,
                            int desired_retention_percent,
                            int learning_step_minutes,
                            int relearning_step_minutes,
                            AnkiScheduleOutcome outcomes[4])
{
    if (!current || !outcomes || card_id == 0) return false;
    if (learning_step_minutes < 1 || learning_step_minutes > 720) {
        learning_step_minutes = 10;
    }
    if (relearning_step_minutes < 1 || relearning_step_minutes > 720) {
        relearning_step_minutes = 10;
    }

    uint8_t status = card_status & ANKI_CARD_STATUS_MASK;
    for (int i = 0; i < 4; i++) {
        AnkiRating rating = (AnkiRating)(i + 1);
        AnkiScheduleOutcome *out = &outcomes[i];
        memset(out, 0, sizeof(*out));
        compute_memory(current, rating, now, &out->schedule);
        out->schedule.magic = ANKI_SCHEDULE_MAGIC;
        out->schedule.version = ANKI_SCHEDULE_VERSION;
        out->schedule.size = sizeof(AnkiScheduleState);
        out->schedule.card_id = card_id;
        out->interval_days = interval_for_stability(
            out->schedule.stability, desired_retention_percent, card_id,
            out->schedule.reps, rating);
        out->schedule.interval_days = out->interval_days;

        if (status == ANKI_CARD_STATUS_NEW && rating != ANKI_RATING_EASY) {
            int minutes;
            out->card_status = ANKI_CARD_STATUS_LEARNING;
            if (rating == ANKI_RATING_AGAIN) {
                out->schedule.learning_step = 0;
                minutes = 1;
            } else if (rating == ANKI_RATING_HARD) {
                out->schedule.learning_step = 0;
                minutes = (learning_step_minutes + 2) / 2;
            } else {
                out->schedule.learning_step = 1;
                minutes = learning_step_minutes;
            }
            out->due_at = add_seconds(now, (uint64_t)minutes * 60U);
        } else if (status == ANKI_CARD_STATUS_LEARNING &&
                   rating != ANKI_RATING_EASY &&
                   !(rating == ANKI_RATING_GOOD &&
                     current->learning_step >= 1)) {
            int minutes;
            out->card_status = ANKI_CARD_STATUS_LEARNING;
            if (rating == ANKI_RATING_AGAIN) {
                out->schedule.learning_step = 0;
                minutes = 1;
            } else if (rating == ANKI_RATING_HARD) {
                minutes = current->learning_step >= 1
                    ? learning_step_minutes + (learning_step_minutes + 1) / 2
                    : (learning_step_minutes + 2) / 2;
            } else {
                out->schedule.learning_step = 1;
                minutes = learning_step_minutes;
            }
            out->due_at = add_seconds(now, (uint64_t)minutes * 60U);
        } else if (status == ANKI_CARD_STATUS_RELEARNING &&
                   rating <= ANKI_RATING_HARD) {
            int minutes = relearning_step_minutes;
            if (rating == ANKI_RATING_HARD) minutes += (minutes + 1) / 2;
            out->card_status = ANKI_CARD_STATUS_RELEARNING;
            out->schedule.learning_step = 0;
            out->due_at = add_seconds(now, (uint64_t)minutes * 60U);
        } else if (status == ANKI_CARD_STATUS_REVIEW &&
                   rating == ANKI_RATING_AGAIN) {
            out->card_status = ANKI_CARD_STATUS_RELEARNING;
            out->schedule.learning_step = 0;
            out->due_at = add_seconds(now,
                                      (uint64_t)relearning_step_minutes * 60U);
            if (out->schedule.lapses < UINT16_MAX) out->schedule.lapses++;
        } else {
            out->card_status = ANKI_CARD_STATUS_REVIEW;
            out->schedule.learning_step = 0;
            out->due_at = add_seconds(now,
                                      (uint64_t)out->interval_days * 86400U);
        }
    }

    if (outcomes[0].interval_days > outcomes[1].interval_days) {
        outcomes[0].interval_days = outcomes[1].interval_days;
    }
    for (int i = 1; i < 4; i++) {
        uint32_t minimum = outcomes[i - 1].interval_days + 1U;
        if (outcomes[i].interval_days < minimum) outcomes[i].interval_days = minimum;
        if (outcomes[i].interval_days > ANKI_FSRS_MAX_INTERVAL_DAYS) {
            outcomes[i].interval_days = ANKI_FSRS_MAX_INTERVAL_DAYS;
        }
        outcomes[i].schedule.interval_days = outcomes[i].interval_days;
        if (outcomes[i].card_status == ANKI_CARD_STATUS_REVIEW) {
            outcomes[i].due_at = add_seconds(now,
                (uint64_t)outcomes[i].interval_days * 86400U);
        }
    }
    outcomes[0].schedule.interval_days = outcomes[0].interval_days;
    return true;
}
