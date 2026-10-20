#include "lv_pomodoro_panel.h"

#include "theme/lv_theme_hardcore.h"

static lv_obj_t *button(lv_obj_t *parent, const char *text, lv_event_cb_t callback)
{
    lv_obj_t *obj = lv_button_create(parent);
    lv_obj_set_size(obj, 150, 54);
    lv_obj_add_event_cb(obj, callback, LV_EVENT_CLICKED, NULL);
    lv_obj_t *label = lv_label_create(obj);
    lv_label_set_text(label, text);
    lv_obj_center(label);
    return label;
}

void lv_pomodoro_panel_create(lv_obj_t *parent, lv_pomodoro_panel_t *panel,
                              lv_event_cb_t toggle_cb, lv_event_cb_t reset_cb)
{
    panel->root = lv_obj_create(parent);
    lv_obj_set_width(panel->root, LV_PCT(100));
    lv_obj_set_flex_grow(panel->root, 1);
    lv_obj_set_flex_flow(panel->root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(panel->root, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(panel->root, 24, 0);

    panel->time = lv_label_create(panel->root);
    lv_obj_set_style_text_font(panel->time, LV_FONT_LARGE, 0);
    panel->state = lv_label_create(panel->root);
    lv_obj_set_style_text_font(panel->state, LV_FONT_NORMAL, 0);

    lv_obj_t *actions = lv_obj_create(panel->root);
    lv_obj_set_width(actions, LV_PCT(100));
    lv_obj_set_height(actions, 72);
    lv_obj_set_style_border_width(actions, 0, 0);
    lv_obj_set_flex_flow(actions, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(actions, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(actions, 20, 0);
    panel->toggle_label = button(actions, "Start", toggle_cb);
    button(actions, "Reset", reset_cb);
}

void lv_pomodoro_panel_render(lv_pomodoro_panel_t *panel, uint32_t remaining_s,
                              const char *state, const char *toggle_text)
{
    if (!panel || !panel->root) return;
    lv_label_set_text_fmt(panel->time, "%02u:%02u",
                          (unsigned)(remaining_s / 60),
                          (unsigned)(remaining_s % 60));
    lv_label_set_text(panel->state, state);
    lv_label_set_text(panel->toggle_label, toggle_text);
    lv_obj_center(panel->toggle_label);
}
