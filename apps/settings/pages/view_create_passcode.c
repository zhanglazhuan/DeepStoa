// view_create_passcode: one navigable page.
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
#include "../widgets/passcode_input.h"
#include "view_create_passcode.h"

typedef struct {
    lv_obj_t *passcode_input1;
    lv_obj_t *passcode_input2;
    lv_obj_t *passcode_hint_label1;
    lv_obj_t *passcode_hint_label2;
    lv_obj_t *passcode_keyboard;
} CreatePasscodeContext;

static void passcode_confirm(lv_event_t *e) {
    SettingsApp *app = lv_event_get_user_data(e);
    CreatePasscodeContext *ctx = (CreatePasscodeContext *)app->view->page_nav.nav_ctx;
    if (!ctx || !ctx->passcode_input1 || !ctx->passcode_input2) return;

    const char *pin1 = lv_textarea_get_text(ctx->passcode_input1);
    const char *pin2 = lv_textarea_get_text(ctx->passcode_input2);

    if (strlen(pin1) != 4 || strlen(pin2) != 4) return;

    if (strcmp(pin1, pin2) == 0) {
        strcpy(app->model->security.security_pin, pin1);
        app->model->security.security_pin_enabled = 1;
        lv_textarea_set_text(ctx->passcode_input1, "");
        lv_textarea_set_text(ctx->passcode_input2, "");
        free(ctx);
        app->view->page_nav.nav_ctx = NULL;
        page_navigator_navigate_pop(&app->view->page_nav, app);
    } else {
        lv_textarea_set_text(ctx->passcode_input1, "");
        lv_textarea_set_text(ctx->passcode_input2, "");
        if (ctx->passcode_hint_label2) {
            lv_obj_clear_flag(ctx->passcode_hint_label2, LV_OBJ_FLAG_HIDDEN);
            lv_label_set_text(ctx->passcode_hint_label2, "Passwords do not match");
        }
    }
}

static void passcode_back(lv_event_t *e) {
    SettingsApp *app = lv_event_get_user_data(e);
    CreatePasscodeContext *ctx = (CreatePasscodeContext *)app->view->page_nav.nav_ctx;
    app->model->security.security_pin_enabled = 0;
    if (ctx) { free(ctx); app->view->page_nav.nav_ctx = NULL; }
    page_navigator_navigate_pop(&app->view->page_nav, app);
}

static lv_obj_t *build_create_passcode_page(struct SettingsApp *app, void *user_data) {
    Page page = lv_page_create("Set Passcode", true,
                               page_navigator_navigate_back, &app->view->page_nav);
    lv_obj_t *cont = page.container;

    CreatePasscodeContext *ctx = (CreatePasscodeContext *)user_data;
    if (!ctx) {
        ctx = malloc(sizeof(CreatePasscodeContext));
        if (!ctx) return NULL;
        memset(ctx, 0, sizeof(*ctx));
    }
    app->view->page_nav.nav_ctx = ctx;

    lv_obj_t *c1 = settings_passcode_input_create(cont, "Enter Passcode");
    lv_obj_t *c2 = settings_passcode_input_create(cont, "Confirm Passcode");
    ctx->passcode_hint_label1 = lv_obj_get_child(c1, 2);
    ctx->passcode_hint_label2 = lv_obj_get_child(c2, 2);
    lv_obj_add_flag(ctx->passcode_hint_label1, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ctx->passcode_hint_label2, LV_OBJ_FLAG_HIDDEN);
    ctx->passcode_input1 = (lv_obj_t *)lv_obj_get_user_data(c1);
    ctx->passcode_input2 = (lv_obj_t *)lv_obj_get_user_data(c2);

    // Buttons
    lv_obj_t *btn_cont = lv_obj_create(cont);
    lv_obj_remove_style_all(btn_cont);
    lv_obj_set_width(btn_cont, LV_PCT(100));
    lv_obj_set_height(btn_cont, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(btn_cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_cont, LV_FLEX_ALIGN_SPACE_AROUND, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *cancel = lv_button_create(btn_cont);
    ui_style_set_btn_secondary(cancel);
    lv_obj_set_size(cancel, 120, 40);
    lv_obj_add_event_cb(cancel, passcode_back, LV_EVENT_CLICKED, app);
    lv_obj_t *cl = lv_label_create(cancel);
    lv_label_set_text(cl, "Cancel"); lv_obj_center(cl);

    lv_obj_t *confirm = lv_button_create(btn_cont);
    ui_style_set_btn_primary(confirm);
    lv_obj_set_size(confirm, 120, 40);
    lv_obj_add_event_cb(confirm, passcode_confirm, LV_EVENT_CLICKED, app);
    lv_obj_t *cf = lv_label_create(confirm);
    lv_label_set_text(cf, "Confirm"); lv_obj_center(cf);

    ctx->passcode_keyboard = lv_keyboard_create(page.screen);
    lv_obj_remove_style_all(ctx->passcode_keyboard);
    lv_keyboard_set_mode(ctx->passcode_keyboard, LV_KEYBOARD_MODE_NUMBER);
    lv_obj_set_width(ctx->passcode_keyboard, LV_PCT(100));
    lv_obj_set_height(ctx->passcode_keyboard, 220);
    lv_obj_align(ctx->passcode_keyboard, LV_ALIGN_BOTTOM_MID, 0, -8);
    if (ctx->passcode_input1)
        lv_keyboard_set_textarea(ctx->passcode_keyboard, ctx->passcode_input1);

    return page.screen;
}

void settings_view_create_passcode_init_registry(struct SettingsApp *app)
{
    PAGE_REGISTE(app, PAGE_SECURITY_CREATE, build_create_passcode_page);
}
