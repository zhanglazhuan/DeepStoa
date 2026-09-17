/*
 * DeepStoa — Home UI (App Icon Grid)
 *
 * Reimplemented from EPOS/Zephyr (epos_home_ui.c) for 480×800 e-ink.
 *
 * Shows a 3-column grid of registered (non-hidden) apps.
 * Click any icon to launch the app via launcher_open_app().
 *
 * Key differences from the old 240×320 version:
 *   - 3 columns (was 2)
 *   - Icon container = cell_width × cell_width (square, dynamic)
 *   - Theme fonts (LV_FONT_SMALL) instead of hardcoded lv_font_montserrat_14
 *   - Accounts for screen padding in layout calculations
 */

#include "esp_log.h"
#include "lvgl.h"
#include "app_manager.h"
#include "lv_page.h"
#include "lv_theme_hardcore.h"
#include "launcher.h"

static const char *TAG = "home_ui";

/* ── App icon click → launch ──────────────────────────────────────────── */

static void app_launcher_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_CLICKED) {
        lv_obj_t *app_btn = lv_event_get_target(e);
        application_t *app = (application_t *)lv_event_get_user_data(e);

        if (app && app->name) {
            /* Highlight selected icon with thicker border */
            lv_obj_t *icon_cont = lv_obj_get_child(app_btn, 0);
            if (icon_cont) {
                lv_obj_set_style_border_width(icon_cont, 3, LV_PART_MAIN);
            }

            ESP_LOGI(TAG, "Launching: %s", app->name);
            lv_async_call(launcher_open_app, (void *)app->name);
        }
    }
}

/* ── Grid layout (3 columns for 480×800 e-ink) ────────────────────────── */

static void create_grid_container(Page *home_page,
                                  lv_coord_t *out_cell_width,
                                  lv_coord_t *out_cell_height,
                                  lv_coord_t *out_internal_gap)
{
    lv_coord_t screen_w = lv_display_get_horizontal_resolution(
                              lv_display_get_default());

    lv_coord_t pad_all    = 10;
    lv_coord_t pad_column = 20;
    lv_coord_t pad_row    = 20;
    lv_coord_t internal_gap = 8;
    lv_coord_t scrollbar_reserve = 15;

    lv_coord_t screen_pad_left  = lv_obj_get_style_pad_left(home_page->screen, 0);
    lv_coord_t screen_pad_right = lv_obj_get_style_pad_right(home_page->screen, 0);

    lv_coord_t available_width = screen_w - (screen_pad_left + screen_pad_right)
                                 - (pad_all * 2) - (pad_column * 2)
                                 - scrollbar_reserve;

    /* 3 columns */
    lv_coord_t cell_width = available_width / 3;

    lv_coord_t font_height = lv_font_get_line_height(LV_FONT_SMALL);

    lv_coord_t cell_height = cell_width + font_height + internal_gap;

    static lv_coord_t col_dsc[] = { LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1),
                                    LV_GRID_TEMPLATE_LAST };
    static lv_coord_t row_dsc[11];
    for (int i = 0; i < 10; i++) {
        row_dsc[i] = cell_height;
    }
    row_dsc[10] = LV_GRID_TEMPLATE_LAST;

    lv_obj_set_layout(home_page->container, LV_LAYOUT_GRID);
    lv_obj_set_style_pad_all(home_page->container, pad_all, LV_PART_MAIN);
    lv_obj_set_style_pad_row(home_page->container, pad_row, LV_PART_MAIN);
    lv_obj_set_style_pad_column(home_page->container, pad_column, LV_PART_MAIN);
    lv_obj_set_grid_dsc_array(home_page->container, col_dsc, row_dsc);

    if (out_cell_width)  *out_cell_width  = cell_width;
    if (out_cell_height) *out_cell_height = cell_height;
    if (out_internal_gap) *out_internal_gap = internal_gap;
}

/* ── Single app grid button ───────────────────────────────────────────── */

static lv_obj_t *create_app_grid_button(lv_obj_t *container, application_t *app,
                                         int col, int row,
                                         lv_coord_t cell_width,
                                         lv_coord_t internal_gap)
{
    lv_obj_t *app_btn = lv_button_create(container);
    lv_obj_set_size(app_btn, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(app_btn, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(app_btn, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(app_btn, 0, LV_PART_MAIN);

    lv_obj_set_flex_flow(app_btn, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(app_btn, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(app_btn, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_row(app_btn, internal_gap, LV_PART_MAIN);

    lv_obj_set_grid_cell(app_btn,
                         LV_GRID_ALIGN_STRETCH, col, 1,
                         LV_GRID_ALIGN_STRETCH, row, 1);

    lv_obj_add_event_cb(app_btn, app_launcher_event_cb, LV_EVENT_CLICKED, app);

    /* Icon container — square, sized to column width */
    lv_obj_t *icon_cont = lv_obj_create(app_btn);
    lv_obj_set_size(icon_cont, cell_width, cell_width);
    lv_obj_set_style_bg_opa(icon_cont, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(icon_cont, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(icon_cont, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_radius(icon_cont, 6, LV_PART_MAIN);
    lv_obj_set_style_pad_all(icon_cont, 0, LV_PART_MAIN);
    lv_obj_clear_flag(icon_cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(icon_cont, LV_OBJ_FLAG_CLICKABLE);

    /* App icon or fallback symbol */
    if (app->icon) {
        lv_obj_t *app_icon = lv_image_create(icon_cont);
        lv_image_set_src(app_icon, app->icon);
        lv_obj_center(app_icon);
    } else {
        lv_obj_t *fallback_icon = lv_label_create(icon_cont);
        lv_label_set_text(fallback_icon, LV_SYMBOL_IMAGE);
        lv_obj_set_style_text_font(fallback_icon, LV_FONT_SMALL, LV_PART_MAIN);
        lv_obj_set_style_text_color(fallback_icon, lv_color_black(), LV_PART_MAIN);
        lv_obj_center(fallback_icon);
    }

    /* App name label */
    lv_obj_t *app_label = lv_label_create(app_btn);
    lv_label_set_text(app_label, app->name);
    lv_obj_set_style_text_font(app_label, LV_FONT_SMALL, LV_PART_MAIN);
    lv_obj_set_style_text_color(app_label, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_width(app_label, cell_width);
    lv_label_set_long_mode(app_label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(app_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

    return app_btn;
}

/* ── Public API ───────────────────────────────────────────────────────── */

void launcher_home_ui(void)
{
    Page home_page = lv_page_create("Applications", false, NULL, NULL);
    lv_obj_del(home_page.header);
    lv_scr_load(home_page.screen);

    lv_coord_t cell_width, cell_height, internal_gap;
    create_grid_container(&home_page, &cell_width, &cell_height, &internal_gap);

    int app_count = app_manager_get_num_apps();

    if (app_count == 0) {
        ESP_LOGW(TAG, "No apps registered");
        lv_obj_t *empty_label = lv_label_create(home_page.container);
        lv_label_set_text(empty_label, "No apps installed");
        lv_obj_set_grid_cell(empty_label, LV_GRID_ALIGN_CENTER, 0, 3,
                             LV_GRID_ALIGN_CENTER, 0, 1);
        return;
    }

    int visible_app_count = 0;
    for (int i = 0; i < app_count; i++) {
        application_t *current_app = app_manager_get_app(i);
        if (current_app == NULL || current_app->hidden) continue;

        int col = visible_app_count % 3;
        int row = visible_app_count / 3;

        create_app_grid_button(home_page.container, current_app,
                               col, row, cell_width, internal_gap);

        visible_app_count++;
    }

    if (visible_app_count == 0) {
        lv_obj_t *empty_label = lv_label_create(home_page.container);
        lv_label_set_text(empty_label, "All apps are hidden");
        lv_obj_set_grid_cell(empty_label, LV_GRID_ALIGN_CENTER, 0, 3,
                             LV_GRID_ALIGN_CENTER, 0, 1);
    }

    ESP_LOGI(TAG, "Home UI ready: %d visible apps", visible_app_count);
}
