#include "lv_todo_list_panel.h"

#include <stdint.h>

static void row_clicked(lv_event_t *event)
{
    lv_obj_t *row = lv_event_get_current_target(event);
    lv_todo_list_panel_t *panel = lv_obj_get_user_data(row);
    if (panel && panel->on_select)
        panel->on_select((size_t)(uintptr_t)lv_event_get_user_data(event),
                         panel->user_data);
}

void lv_todo_list_panel_create(lv_obj_t *parent, lv_todo_list_panel_t *panel,
                               lv_event_cb_t refresh_cb, void *refresh_data)
{
    panel->root = lv_obj_create(parent);
    lv_obj_set_width(panel->root, LV_PCT(100));
    lv_obj_set_flex_grow(panel->root, 1);
    lv_obj_set_flex_flow(panel->root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(panel->root, 8, 0);

    panel->status = lv_label_create(panel->root);
    lv_obj_set_width(panel->status, LV_PCT(100));
    lv_label_set_long_mode(panel->status, LV_LABEL_LONG_WRAP);
    panel->list = lv_obj_create(panel->root);
    lv_obj_set_width(panel->list, LV_PCT(100));
    lv_obj_set_flex_grow(panel->list, 1);
    lv_obj_set_flex_flow(panel->list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(panel->list, 4, 0);

    lv_obj_t *refresh = lv_button_create(panel->root);
    lv_obj_set_size(refresh, 140, 48);
    lv_obj_add_event_cb(refresh, refresh_cb, LV_EVENT_CLICKED, refresh_data);
    lv_obj_t *label = lv_label_create(refresh);
    lv_label_set_text(label, "Refresh");
    lv_obj_center(label);
}

void lv_todo_list_panel_render(lv_todo_list_panel_t *panel,
                               const lv_todo_row_t *rows, size_t count,
                               const char *status, lv_todo_select_cb_t on_select,
                               void *user_data)
{
    if (!panel || !panel->list) return;
    panel->on_select = on_select;
    panel->user_data = user_data;
    lv_label_set_text(panel->status, status ? status : "");
    lv_obj_clean(panel->list);
    for (size_t i = 0; i < count; ++i) {
        lv_obj_t *row = lv_button_create(panel->list);
        lv_obj_set_width(row, LV_PCT(100));
        lv_obj_set_height(row, 48);
        lv_obj_set_user_data(row, panel);
        lv_obj_add_event_cb(row, row_clicked, LV_EVENT_CLICKED,
                            (void *)(uintptr_t)i);
        lv_obj_t *label = lv_label_create(row);
        lv_obj_set_width(label, LV_PCT(96));
        lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
        lv_label_set_text_fmt(label, "%s%s", rows[i].completed ? "[x] " : "[ ] ",
                              rows[i].content ? rows[i].content : "");
        lv_obj_center(label);
    }
}
