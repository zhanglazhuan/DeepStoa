#include "storage.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "sd_control.h"
#include "model.h"

#define TODO_DIR "/sdcard/lxj_todolist"
#define TODO_FILE TODO_DIR "/todolist.csv"
#define TODO_TEMP TODO_DIR "/todolist.tmp"
#define LEGACY_FILE "/sdcard/todolist/todolist.csv"

static const char *TAG = "tovo_storage";

static bool load_file(TovoModel *model, const char *path, bool legacy)
{
    FILE *file = fopen(path, "r");
    if (!file) {
        if (errno != ENOENT) model->load_error = true;
        return false;
    }
    char line[384];
    unsigned scanned = 0;
    while (fgets(line, sizeof(line), file)) {
        if (++scanned > 256) { model->read_only = true; break; }
        if (!strchr(line, '\n')) {
            int ch = fgetc(file);
            if (ch != EOF) {
                while (ch != '\n' && ch != EOF) ch = fgetc(file);
                model->read_only = true;
                continue;
            }
        }
        if (model->count >= TOVO_MAX) {
            model->read_only = true;
            break;
        }
        char *save = NULL;
        char *field = strtok_r(line, ",\r\n", &save);
        if (!field) continue;
        char *content = strtok_r(NULL, ",\r\n", &save);
        if (!content || !content[0]) {
            model->read_only = true;
            continue;
        }
        if (strlen(content) >= TOVO_CONTENT_SIZE) {
            model->read_only = true;
            continue;
        }
        TovoItem *item = &model->items[model->count];
        snprintf(item->content, sizeof(item->content), "%s", content);
        item->completed = false;
        if (legacy) {
            for (int index = 2; index <= 14; ++index) {
                field = strtok_r(NULL, ",\r\n", &save);
                if (!field) { model->read_only = true; break; }
                if (index == 14) item->completed = atoi(field) != 0;
            }
        } else {
            item->completed = atoi(field) != 0;
            if (strtok_r(NULL, ",\r\n", &save)) model->read_only = true;
        }
        ++model->count;
    }
    if (ferror(file)) model->load_error = true;
    if (fclose(file) != 0) model->load_error = true;
    return true;
}

void tovo_storage_load(TovoModel *model)
{
    if (!load_file(model, TODO_FILE, false) && !model->load_error)
        load_file(model, LEGACY_FILE, true);
    if (model->load_error || model->read_only)
        ESP_LOGW(TAG, "CSV requires attention (read_error=%d, read_only=%d)",
                 model->load_error, model->read_only);
}

bool tovo_storage_save(const TovoModel *model)
{
    if (!sd_control_check_exists(TODO_DIR) && !sd_control_create_dir(TODO_DIR))
        return false;
    FILE *file = fopen(TODO_TEMP, "w");
    if (!file) return false;
    bool ok = true;
    for (size_t i = 0; i < model->count; ++i) {
        if (fprintf(file, "%d,%s\n", model->items[i].completed ? 1 : 0,
                    model->items[i].content) < 0) { ok = false; break; }
    }
    if (fflush(file) != 0) ok = false;
    if (fclose(file) != 0) ok = false;
    if (ok && rename(TODO_TEMP, TODO_FILE) == 0) return true;
    ESP_LOGE(TAG, "could not save CSV: %d", errno);
    remove(TODO_TEMP);
    return false;
}
