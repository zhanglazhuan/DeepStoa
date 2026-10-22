#include "lv_floating_button.h"

#include "lv_theme_hardcore.h"
#include "lv_ui_style_guide.h"

static lv_obj_t *create_button(lv_obj_t *parent, lv_event_cb_t callback,
                               void *user_data)
{
    lv_obj_t *button = lv_button_create(parent);
    ui_style_set_btn_primary(button);
    lv_obj_set_size(button, LV_FLOATING_BUTTON_SIZE, LV_FLOATING_BUTTON_SIZE);
    lv_obj_set_style_radius(button, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(button, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(button, 0, LV_PART_MAIN);
    lv_obj_set_ext_click_area(button, 8);
    lv_obj_add_flag(button, LV_OBJ_FLAG_IGNORE_LAYOUT | LV_OBJ_FLAG_FLOATING);
    lv_obj_clear_flag(button, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(button, LV_ALIGN_BOTTOM_RIGHT,
                 -LV_FLOATING_BUTTON_MARGIN, -LV_FLOATING_BUTTON_MARGIN);

    if (callback) lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, user_data);
    return button;
}

lv_obj_t *lv_floating_button_create(lv_obj_t *parent, const char *symbol,
                                    lv_event_cb_t callback, void *user_data)
{
    lv_obj_t *button = create_button(parent, callback, user_data);
    lv_obj_t *icon = lv_label_create(button);
    lv_label_set_text(icon, symbol);
    lv_obj_set_style_text_font(icon, LV_FONT_NORMAL, LV_PART_MAIN);
    lv_obj_set_style_text_color(icon, lv_color_white(), LV_PART_MAIN);
    lv_obj_center(icon);

    return button;
}

lv_obj_t *lv_floating_button_create_image(lv_obj_t *parent,
                                          const lv_image_dsc_t *image,
                                          lv_event_cb_t callback, void *user_data)
{
    lv_obj_t *button = create_button(parent, callback, user_data);
    lv_obj_t *icon = lv_image_create(button);
    lv_image_set_src(icon, image);
    lv_obj_set_style_image_recolor(icon, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_image_recolor_opa(icon, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_center(icon);
    return button;
}
