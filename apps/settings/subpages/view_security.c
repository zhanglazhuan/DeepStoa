// apps/settings/subpages/view_security.c
// Security settings page — Phase 1 (PIN + personal info)
// Ported from D:\Codes\EPOS\epos\apps\settings\subpages\view_security.c

#include <string.h>
#include <stdlib.h>
#include <lvgl.h>
#include "lv_ui_style_guide.h"
#include "lv_keyboard.h"
#include "esp_log.h"
#include "lv_page.h"
#include "page_navigator.h"
#include "settings_app.h"
#include "settings_model.h"
#include "settings_view.h"
#include "settings_controller.h"

// ── Context structs ────────────────────────────────────────────────────

typedef struct {
    lv_obj_t *passcode_switch;
    lv_obj_t *change_passcode_row;
    lv_obj_t *personal_info_input;
    lv_obj_t *personal_info_keyboard;
} SecurityPageContext;

typedef struct {
    lv_obj_t *passcode_input1;
    lv_obj_t *passcode_input2;
    lv_obj_t *passcode_hint_label1;
    lv_obj_t *passcode_hint_label2;
    lv_obj_t *passcode_keyboard;
} CreatePasscodeContext;

typedef struct {
    lv_obj_t *passcode_input3;
    lv_obj_t *passcode_hint_label3;
    lv_obj_t *passcode_keyboard;
    int verify_purpose;
} VerifyPasscodeContext;

// ── Helpers ────────────────────────────────────────────────────────────

static lv_obj_t *create_password_input_component(lv_obj_t *parent,
                                                  const char *title_text,
                                                  struct SettingsApp *app) {
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

// ── Forward declarations ───────────────────────────────────────────────

static void passcode_toggle_changed(lv_event_t *e);
static void passcode_confirm(lv_event_t *e);
static void passcode_back(lv_event_t *e);
static void personal_info_toggle_changed(lv_event_t *e);
static void change_passcode_clicked(lv_event_t *e);
static void verify_passcode_confirm(lv_event_t *e);
static void verify_passcode_back(lv_event_t *e);

// ── Passcode handlers ──────────────────────────────────────────────────

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
            VerifyPasscodeContext *vc = malloc(sizeof(VerifyPasscodeContext));
            vc->verify_purpose = 1;
            PAGE_NAVIGATE_TO(app, PAGE_SECURITY_VERIFY, vc);
        } else {
            if (ctx->change_passcode_row)
                lv_obj_add_flag(ctx->change_passcode_row, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

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

// ── Personal info handlers ─────────────────────────────────────────────

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

// ── Verify passcode ────────────────────────────────────────────────────

static void change_passcode_clicked(lv_event_t *e) {
    SettingsApp *app = lv_event_get_user_data(e);
    VerifyPasscodeContext *vc = malloc(sizeof(VerifyPasscodeContext));
    vc->verify_purpose = 2;
    PAGE_NAVIGATE_TO(app, PAGE_SECURITY_VERIFY, vc);
}

static void verify_passcode_back(lv_event_t *e) {
    SettingsApp *app = lv_event_get_user_data(e);
    VerifyPasscodeContext *ctx = (VerifyPasscodeContext *)app->view->page_nav.nav_ctx;
    if (ctx) { free(ctx); app->view->page_nav.nav_ctx = NULL; }
    page_navigator_navigate_pop(&app->view->page_nav, app);
}

static void verify_passcode_confirm(lv_event_t *e) {
    SettingsApp *app = lv_event_get_user_data(e);
    VerifyPasscodeContext *ctx = (VerifyPasscodeContext *)app->view->page_nav.nav_ctx;
    if (!ctx || !ctx->passcode_input3) return;

    const char *pin = lv_textarea_get_text(ctx->passcode_input3);
    if (strcmp(pin, app->model->security.security_pin) != 0) return;

    if (ctx->verify_purpose == 1) {
        app->model->security.security_pin_enabled = 0;
        memset(app->model->security.security_pin, 0, sizeof(app->model->security.security_pin));
        free(ctx); app->view->page_nav.nav_ctx = NULL;
        page_navigator_navigate_pop(&app->view->page_nav, app);
    } else if (ctx->verify_purpose == 2) {
        free(ctx); app->view->page_nav.nav_ctx = NULL;
        CreatePasscodeContext *cc = malloc(sizeof(CreatePasscodeContext));
        memset(cc, 0, sizeof(*cc));
        PAGE_NAVIGATE_TO(app, PAGE_SECURITY_CREATE, cc);
    }
}

// ── Page builders ──────────────────────────────────────────────────────

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

    lv_obj_t *c1 = create_password_input_component(cont, "Enter Passcode", app);
    lv_obj_t *c2 = create_password_input_component(cont, "Confirm Passcode", app);
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

static lv_obj_t *build_verify_passcode_page(struct SettingsApp *app, void *user_data) {
    Page page = lv_page_create("Verify Passcode", true,
                               page_navigator_navigate_back, &app->view->page_nav);
    lv_obj_t *cont = page.container;

    VerifyPasscodeContext *ctx = (VerifyPasscodeContext *)user_data;
    if (!ctx) {
        ctx = malloc(sizeof(VerifyPasscodeContext));
        if (!ctx) return NULL;
        memset(ctx, 0, sizeof(*ctx));
    }
    app->view->page_nav.nav_ctx = ctx;

    lv_obj_t *c = create_password_input_component(cont, "Enter Passcode", app);
    ctx->passcode_hint_label3 = lv_obj_get_child(c, 2);
    lv_obj_add_flag(ctx->passcode_hint_label3, LV_OBJ_FLAG_HIDDEN);
    ctx->passcode_input3 = (lv_obj_t *)lv_obj_get_user_data(c);

    lv_obj_t *btn_cont = lv_obj_create(cont);
    lv_obj_remove_style_all(btn_cont);
    lv_obj_set_width(btn_cont, LV_PCT(100));
    lv_obj_set_height(btn_cont, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(btn_cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_cont, LV_FLEX_ALIGN_SPACE_AROUND, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *cancel = lv_button_create(btn_cont);
    ui_style_set_btn_secondary(cancel);
    lv_obj_set_size(cancel, 100, 40);
    lv_obj_add_event_cb(cancel, verify_passcode_back, LV_EVENT_CLICKED, app);
    lv_obj_t *cl = lv_label_create(cancel);
    lv_label_set_text(cl, "Cancel"); lv_obj_center(cl);

    lv_obj_t *confirm = lv_button_create(btn_cont);
    ui_style_set_btn_primary(confirm);
    lv_obj_set_size(confirm, 100, 40);
    lv_obj_add_event_cb(confirm, verify_passcode_confirm, LV_EVENT_CLICKED, app);
    lv_obj_t *cf = lv_label_create(confirm);
    lv_label_set_text(cf, "Confirm"); lv_obj_center(cf);

    ctx->passcode_keyboard = lv_keyboard_create(page.screen);
    lv_obj_remove_style_all(ctx->passcode_keyboard);
    lv_keyboard_set_mode(ctx->passcode_keyboard, LV_KEYBOARD_MODE_NUMBER);
    lv_obj_set_width(ctx->passcode_keyboard, LV_PCT(100));
    lv_obj_set_height(ctx->passcode_keyboard, 220);
    lv_obj_align(ctx->passcode_keyboard, LV_ALIGN_BOTTOM_MID, 0, -8);
    if (ctx->passcode_input3)
        lv_keyboard_set_textarea(ctx->passcode_keyboard, ctx->passcode_input3);

    return page.screen;
}

void settings_view_security_init_registry(SettingsApp *app) {
    PAGE_REGISTE(app, PAGE_SECURITY, build_security_page);
    PAGE_REGISTE(app, PAGE_SECURITY_CREATE, build_create_passcode_page);
    PAGE_REGISTE(app, PAGE_SECURITY_VERIFY, build_verify_passcode_page);
}
