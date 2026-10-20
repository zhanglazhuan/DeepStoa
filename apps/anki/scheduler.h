#ifndef ANKI_SCHEDULER_H
#define ANKI_SCHEDULER_H

#include <stdbool.h>
#include <stdint.h>

#define ANKI_SCHEDULE_MAGIC   UINT32_C(0x46535253)
#define ANKI_SCHEDULE_VERSION UINT16_C(1)
#define ANKI_FSRS_MAX_INTERVAL_DAYS UINT32_C(36500)

typedef enum {
    ANKI_RATING_AGAIN = 1,
    ANKI_RATING_HARD = 2,
    ANKI_RATING_GOOD = 3,
    ANKI_RATING_EASY = 4,
} AnkiRating;

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t size;
    uint32_t card_id;
    uint32_t last_review_at;
    uint32_t interval_days;
    float stability;
    float difficulty;
    uint16_t reps;
    uint16_t lapses;
    uint8_t learning_step;
    uint8_t reserved[3];
} AnkiScheduleState;

typedef struct {
    AnkiScheduleState schedule;
    uint32_t due_at;
    uint32_t interval_days;
    uint8_t card_status;
} AnkiScheduleOutcome;

bool anki_scheduler_preview(const AnkiScheduleState *current,
                            uint8_t card_status,
                            uint32_t card_id,
                            uint32_t now,
                            int desired_retention_percent,
                            int learning_step_minutes,
                            int relearning_step_minutes,
                            AnkiScheduleOutcome outcomes[4]);

float anki_scheduler_retrievability(const AnkiScheduleState *state,
                                    uint32_t now);

#endif
