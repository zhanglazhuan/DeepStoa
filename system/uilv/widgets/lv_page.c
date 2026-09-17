/**
 * @file lv_page.c
 * @brief Standard page skeleton — status bar spacer + Header (back | title | right) + Container
 *
 * Dimensions aligned with D:\Codes\EPOS\epos\epos_lv\widgets\lv_page.c
 */
#include <stdio.h>
#include <string.h>
#include "lv_page.h"
#include "lv_theme_hardcore.h"
#include "esp_log.h"

static const char *TAG = "lv_page";

Page lv_page_create(const char *title, bool allow_back, lv_event_cb_t back_cb, void *user_data)
{
    Page page;
    memset(&page, 0, sizeof(page));

    /* 1. Screen — strip theme padding; spacer handles top, container fills rest */
    page.screen = lv_obj_create(NULL);
    lv_obj_set_style_pad_all(page.screen, 0, 0);
    lv_obj_set_flex_flow(page.screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scrollbar_mode(page.screen, LV_SCROLLBAR_MODE_OFF);

    /* 2. Status bar spacer (DeepStoa-specific: global status bar on lv_layer_top) */
    {
        lv_obj_t *spacer = lv_obj_create(page.screen);
        lv_obj_remove_style_all(spacer);
        lv_obj_set_size(spacer, LV_PCT(100), LV_STATUS_BAR_HEIGHT);
    }

    /* 3. Header — 50px height, flex row, matches EPOS */
    page.header = lv_obj_create(page.screen);
    lv_obj_set_width(page.header, LV_PCT(100));
    lv_obj_set_height(page.header, LV_PAGE_HEADER_HEIGHT);
    lv_obj_set_scroll_dir(page.header, LV_DIR_NONE);
    lv_obj_set_style_border_width(page.header, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(page.header, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(page.header, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_flex_flow(page.header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(page.header, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    /* 4. Back button (or spacer) — 40×40, matches EPOS */
    lv_obj_t *btn_back;
    if (allow_back) {
        btn_back = lv_button_create(page.header);
        lv_obj_set_size(btn_back, 40, 40);
        lv_obj_set_style_border_width(btn_back, 0, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(btn_back, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_text_color(btn_back, lv_color_black(), 0);
        lv_obj_set_style_shadow_width(btn_back, 0, 0);
        lv_obj_set_style_pad_all(btn_back, 0, 0);

        lv_obj_t *lbl = lv_label_create(btn_back);
        lv_label_set_text(lbl, LV_SYMBOL_LEFT);
        lv_obj_center(lbl);

        if (back_cb) {
            lv_obj_add_event_cb(btn_back, back_cb, LV_EVENT_CLICKED, user_data);
        }
    } else {
        btn_back = lv_obj_create(page.header);
        lv_obj_remove_style_all(btn_back);
        lv_obj_set_size(btn_back, 40, 40);
    }

    /* 5. Title — 32px Montserrat, flex-grow to fill remaining header space */
    lv_obj_t *title_lbl;
    if (title) {
        title_lbl = lv_label_create(page.header);
        lv_label_set_text(title_lbl, title);
        lv_obj_set_flex_grow(title_lbl, 1);
        lv_obj_set_style_text_align(title_lbl, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_font(title_lbl, LV_FONT_NORMAL, 0);
    } else {
        title_lbl = lv_obj_create(page.header);
        lv_obj_remove_style_all(title_lbl);
        lv_obj_set_flex_grow(title_lbl, 1);
    }

    /* 6. Right slot — 40×40 placeholder, matches EPOS */
    lv_obj_t *right_slot = lv_obj_create(page.header);
    lv_obj_remove_style_all(right_slot);
    lv_obj_set_size(right_slot, 40, 40);
    page.header_right = right_slot;

    /* 7. Container — flex_grow: 1 fills remaining space, matches EPOS */
    page.container = lv_obj_create(page.screen);
    lv_obj_set_width(page.container, LV_PCT(100));
    lv_obj_set_style_border_width(page.container, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(page.container, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(page.container, LV_PAGE_HOR_PAD, LV_PART_MAIN);
    lv_obj_set_flex_grow(page.container, 1);
    lv_obj_set_flex_flow(page.container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scrollbar_mode(page.container, LV_SCROLLBAR_MODE_OFF);

    ESP_LOGI(TAG, "create: \"%s\" (%sback)",
             title ? title : "(no title)", allow_back ? "" : "no ");
    return page;
}
