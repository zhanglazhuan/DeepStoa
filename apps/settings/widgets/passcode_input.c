#include "passcode_input.h"
#include "lv_ui_style_guide.h"
#include "lv_theme_hardcore.h"

lv_obj_t *settings_passcode_input_create(lv_obj_t *parent,
                                        const char *title_text) {
    lv_obj_t *cont = lv_obj_create(parent);
    lv_obj_remove_style_all(cont);
    lv_obj_set_width(cont, LV_PCT(100));
    lv_obj_set_height(cont, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    lv_obj_t *title_label = lv_label_create(cont);
    lv_label_set_text(title_label, title_text);
    lv_obj_set_style_text_font(title_label, LV_FONT_SMALL, LV_PART_MAIN);

    lv_obj_t *input = lv_textarea_create(cont);
    lv_textarea_set_one_line(input, true);
    lv_textarea_set_password_mode(input, true);
    lv_obj_set_width(input, 200);
    lv_obj_set_height(input, LV_SIZE_CONTENT);
    lv_obj_set_scroll_dir(input, LV_DIR_NONE);
    lv_textarea_set_max_length(input, 4);
    lv_textarea_set_placeholder_text(input, "Enter 4 digits");
    lv_obj_set_style_text_font(input, LV_FONT_SMALL, LV_PART_MAIN);

    lv_obj_t *hint = lv_label_create(cont);
    lv_label_set_text(hint, "Two passwords must match");
    lv_obj_set_style_text_font(hint, LV_FONT_SMALL, LV_PART_MAIN);

    lv_obj_set_user_data(cont, input);
    return cont;
}
