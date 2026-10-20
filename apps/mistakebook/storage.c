#include "storage.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"

static const char *TAG = "mistakebook_store";
static const char *CARD_PATH = "/sdcard/mistakebook/cards.csv";
static const char *TEMP_PATH = "/sdcard/mistakebook/cards.csv.tmp";

/* Splitting in place keeps empty CSV fields in their original positions. */
static bool parse_card(char *line, MistakebookCard *card)
{
    char *fields[9] = {0};
    size_t count = 0;
    fields[count++] = line;
    for (char *p = line; *p; ++p) {
        if (*p != '|') continue;
        if (count == 9) return false;
        *p = '\0';
        fields[count++] = p + 1;
    }
    if (count != 5 && count != 9) return false;
    for (size_t i = 0; i < count; ++i) {
        if (strlen(fields[i]) >= MISTAKEBOOK_FIELD_LEN) return false;
    }
    memset(card, 0, sizeof(*card));
    card->legacy_format = count == 5;
    snprintf(card->subject, sizeof(card->subject), "%s", fields[0]);
    snprintf(card->title, sizeof(card->title), "%s", fields[1]);
    if (count == 5) {
        snprintf(card->knowledge_point, sizeof(card->knowledge_point), "%s", fields[2]);
        snprintf(card->error_type, sizeof(card->error_type), "%s", fields[3]);
        card->review_pending = fields[4][0] ? atoi(fields[4]) != 0 : true;
    } else {
        snprintf(card->question, sizeof(card->question), "%s", fields[2]);
        snprintf(card->student_answer, sizeof(card->student_answer), "%s", fields[3]);
        snprintf(card->correct_answer, sizeof(card->correct_answer), "%s", fields[4]);
        snprintf(card->knowledge_point, sizeof(card->knowledge_point), "%s", fields[5]);
        snprintf(card->error_type, sizeof(card->error_type), "%s", fields[6]);
        snprintf(card->explanation, sizeof(card->explanation), "%s", fields[7]);
        card->review_pending = fields[8][0] ? atoi(fields[8]) != 0 : true;
    }
    return true;
}

void mistakebook_storage_load(MistakebookModel *model)
{
    FILE *file = fopen(CARD_PATH, "r");
    if (!file) {
        if (errno != ENOENT) {
            model->load_error = true;
            ESP_LOGW(TAG, "cannot open cards.csv: %d", errno);
        }
        return;
    }
    char line[2048];
    while (fgets(line, sizeof(line), file)) {
        size_t len = strlen(line);
        if (len && line[len - 1] != '\n' && !feof(file)) {
            model->read_only = true;
            int ch;
            while ((ch = fgetc(file)) != '\n' && ch != EOF) {}
            continue;
        }
        line[strcspn(line, "\r\n")] = '\0';
        if (!line[0]) continue;
        if (model->count == MISTAKEBOOK_MAX_CARDS) {
            model->read_only = true;
            continue;
        }
        if (!parse_card(line, &model->cards[model->count])) {
            model->read_only = true;
            continue;
        }
        ++model->count;
    }
    if (ferror(file)) model->load_error = true;
    if (fclose(file) != 0) model->load_error = true;
    if (model->read_only) ESP_LOGW(TAG, "source contains unsupported rows; review writes disabled");
}

bool mistakebook_storage_save(const MistakebookModel *model)
{
    if (!model || model->read_only || model->load_error) return false;
    FILE *file = fopen(TEMP_PATH, "w");
    if (!file) return false;
    bool ok = true;
    for (uint32_t i = 0; i < model->count; ++i) {
        const MistakebookCard *card = &model->cards[i];
        int written = card->legacy_format
            ? fprintf(file, "%s|%s|%s|%s|%d\n",
                      card->subject, card->title, card->knowledge_point,
                      card->error_type, card->review_pending ? 1 : 0)
            : fprintf(file, "%s|%s|%s|%s|%s|%s|%s|%s|%d\n",
                      card->subject, card->title, card->question,
                      card->student_answer, card->correct_answer,
                      card->knowledge_point, card->error_type,
                      card->explanation, card->review_pending ? 1 : 0);
        if (written < 0) {
            ok = false;
            break;
        }
    }
    if (fclose(file) != 0) ok = false;
    if (ok) ok = rename(TEMP_PATH, CARD_PATH) == 0;
    if (!ok) {
        remove(TEMP_PATH);
        ESP_LOGW(TAG, "failed to save cards.csv");
    }
    return ok;
}
