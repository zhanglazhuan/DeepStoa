// view_security: one navigable page.
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <lvgl.h>
#include "lv_ui_style_guide.h"
#include "lv_keyboard.h"
#include "lv_page.h"
#include "page_navigator.h"
#include "settings_app.h"
#include "settings_model.h"
#include "settings_view.h"
#include "view_security.h"

typedef struct {
    lv_obj_t *passcode_switch;
    lv_obj_t *change_passcode_row;
    lv_obj_t *personal_info_input;
    lv_obj_t *personal_info_keyboard;
} SecurityPageContext;

static void passcode_toggle_changed(lv_event_t *e) {
    SettingsApp *app = lv_event_get_user_data(e);
    SecurityPageContext *ctx = (SecurityPageContext *)app->view->page_nav.nav_ctx;
    if (!ctx) return;

    bool enabled = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
    app->model->security.security_pin_enabled = enabled;

    if (enabled) {
        if (strlen(app->model->security.security_pin) > 0) return;
        PAGE_NAVIGATE_TO(app, PAGE_SECURITY_CREATE, NULL);
    } else {
        if (strlen(app->model->security.security_pin) > 0) {
            PAGE_NAVIGATE_TO(app, PAGE_SECURITY_VERIFY, (void *)(intptr_t)1);
        } else {
            if (ctx->change_passcode_row)
                lv_obj_add_flag(ctx->change_passcode_row, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static void personal_info_toggle_changed(lv_event_t *e) {
    SettingsApp *app = lv_event_get_user_data(e);
    SecurityPageContext *ctx = (SecurityPageContext *)app->view->page_nav.nav_ctx;
    if (!ctx) return;
    app->model->security.personal_info_enabled =
        lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
    if (app->model->security.personal_info_enabled) {
        lv_obj_clear_flag(ctx->personal_info_input, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(ctx->personal_info_input, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ctx->personal_info_keyboard, LV_OBJ_FLAG_HIDDEN);
    }
}

static void change_passcode_clicked(lv_event_t *e) {
    SettingsApp *app = lv_event_get_user_data(e);
    PAGE_NAVIGATE_TO(app, PAGE_SECURITY_VERIFY, (void *)(intptr_t)2);
}

static lv_obj_t *build_security_page(struct SettingsApp *app, void *user_data) {
    (void)user_data;
    Page page = lv_page_create("Security", true,
                               page_navigator_navigate_back, &app->view->page_nav);
    lv_obj_t *cont = page.container;

    SecurityPageContext *ctx = malloc(sizeof(SecurityPageContext));
    memset(ctx, 0, sizeof(*ctx));
    app->view->page_nav.nav_ctx = ctx;

    // Passcode row
    lv_obj_t *prow = lv_obj_create(cont);
    lv_obj_remove_style_all(prow);
    lv_obj_set_width(prow, LV_PCT(100));
    lv_obj_set_height(prow, 44);
    lv_obj_set_flex_flow(prow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(prow, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(prow, 0, 0);
    lv_obj_set_style_margin_left(prow, 24, 0);

    lv_obj_t *pl = lv_label_create(prow);
    lv_label_set_text(pl, "Passcode");
    ctx->passcode_switch = lv_switch_create(prow);
    ui_style_set_switch(ctx->passcode_switch);
    if (app->model->security.security_pin_enabled)
        lv_obj_add_state(ctx->passcode_switch, LV_STATE_CHECKED);
    lv_obj_add_event_cb(ctx->passcode_switch, passcode_toggle_changed,
                        LV_EVENT_VALUE_CHANGED, app);

    // Change passcode row
    lv_obj_t *crow = lv_obj_create(cont);
    lv_obj_remove_style_all(crow);
    lv_obj_set_width(crow, LV_PCT(100));
    lv_obj_set_height(crow, 44);
    lv_obj_set_flex_flow(crow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(crow, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(crow, 0, 0);
    lv_obj_set_style_margin_left(crow, 24, 0);

    lv_obj_t *cl = lv_label_create(crow);
    lv_label_set_text(cl, "Change Passcode");
    lv_obj_t *cbtn = lv_button_create(crow);
    lv_obj_remove_style_all(cbtn);
    lv_obj_add_event_cb(cbtn, change_passcode_clicked, LV_EVENT_CLICKED, app);
    lv_obj_t *arr = lv_label_create(cbtn);
    lv_label_set_text(arr, LV_SYMBOL_RIGHT);
    lv_obj_center(arr);
    ctx->change_passcode_row = crow;
    if (!(app->model->security.security_pin_enabled &&
          strlen(app->model->security.security_pin) > 0)) {
        lv_obj_add_flag(crow, LV_OBJ_FLAG_HIDDEN);
    }

    // Personal info row
    lv_obj_t *pi_cont = lv_obj_create(cont);
    lv_obj_remove_style_all(pi_cont);
    lv_obj_set_width(pi_cont, LV_PCT(100));
    lv_obj_set_flex_flow(pi_cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(pi_cont, 0, 0);
    lv_obj_set_style_margin_left(pi_cont, 24, 0);

    lv_obj_t *pi_row = lv_obj_create(pi_cont);
    lv_obj_remove_style_all(pi_row);
    lv_obj_set_width(pi_row, LV_PCT(100));
    lv_obj_set_height(pi_row, 44);
    lv_obj_set_flex_flow(pi_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(pi_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(pi_row, 0, 0);

    lv_obj_t *pil = lv_label_create(pi_row);
    lv_label_set_text(pil, "Personal Information");
    lv_obj_t *pis = lv_switch_create(pi_row);
    ui_style_set_switch(pis);
    if (app->model->security.personal_info_enabled)
        lv_obj_add_state(pis, LV_STATE_CHECKED);
    lv_obj_add_event_cb(pis, personal_info_toggle_changed, LV_EVENT_VALUE_CHANGED, app);

    ctx->personal_info_input = lv_textarea_create(pi_cont);
    lv_obj_set_width(ctx->personal_info_input, LV_PCT(100));
    /* 高度交给下面的 lv_textarea_set_one_line()（LV_SIZE_CONTENT）。
     * 这里原本写死的 32 本就被 set_one_line 覆盖，留着只会误导。 */
    lv_obj_set_style_border_width(ctx->personal_info_input, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(ctx->personal_info_input, 2, LV_PART_ITEMS);
    lv_obj_set_style_bg_opa(ctx->personal_info_input, LV_OPA_0, LV_PART_MAIN);
    lv_obj_add_flag(ctx->personal_info_input, LV_OBJ_FLAG_HIDDEN);
    lv_textarea_set_one_line(ctx->personal_info_input, true);
    lv_textarea_set_placeholder_text(ctx->personal_info_input, "Enter your name");

    ctx->personal_info_keyboard = lv_keyboard_create(page.screen);
    setup_custom_keyboard(ctx->personal_info_keyboard);
    lv_obj_add_flag(ctx->personal_info_keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_size(ctx->personal_info_keyboard, LV_PCT(100), 200);
    lv_obj_align(ctx->personal_info_keyboard, LV_ALIGN_BOTTOM_MID, 0, -8);
    lv_keyboard_set_textarea(ctx->personal_info_keyboard, ctx->personal_info_input);

    return page.screen;
}

void settings_view_security_init_registry(struct SettingsApp *app)
{
    PAGE_REGISTE(app, PAGE_SECURITY, build_security_page);
}
