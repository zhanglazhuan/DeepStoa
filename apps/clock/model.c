// apps/clock/model.c
// Clock model —— 只剩 UI 状态，业务数据都在 system/alarm。

#include <stdlib.h>
#include <string.h>

#include "esp_log.h"

#include "clock_app.h"
#include "model.h"

static const char *TAG = "clock_model";

void clock_model_init(ClockApp *app)
{
    app->model = malloc(sizeof(ClockModel));
    if (!app->model) {
        ESP_LOGE(TAG, "Failed to alloc ClockModel");
        return;
    }
    memset(app->model, 0, sizeof(ClockModel));
    app->model->edit_alarm_idx = -1;
}

void clock_model_deinit(ClockApp *app)
{
    if (app->model) {
        free(app->model);
        app->model = NULL;
    }
}
