/* DeepStoa launcher: two pages, four rows and three columns per page. */
#include <stdint.h>
#include "esp_log.h"
#include "lvgl.h"
#include "app_manager.h"
#include "lv_page.h"
#include "lv_theme_hardcore.h"
#include "launcher.h"

#define HOME_COLUMNS 3
#define HOME_ROWS 4
#define HOME_PAGE_SIZE (HOME_COLUMNS * HOME_ROWS)
#define HOME_MAX_PAGES 2

static const char *TAG = "home_ui";
static struct {
    int current_page;
    int page_count;
    lv_obj_t *grid;
    lv_coord_t icon_size;
} s_home_ctx;

static void show_page(int page);

static void show_page_async(void *page)
{
    show_page((int)(intptr_t)page);
}

static void app_launcher_event_cb(lv_event_t *e)
{
    application_t *app = lv_event_get_user_data(e);
    if (!app || !app->name) return;

    lv_obj_t *icon = lv_obj_get_child(lv_event_get_target(e), 0);
    if (icon) lv_obj_set_style_border_width(icon, 3, LV_PART_MAIN);

    /* The selected page remains in s_home_ctx until the next visit. */
    ESP_LOGI(TAG, "Launching: %s from page %d", app->name, s_home_ctx.current_page + 1);
    lv_async_call(launcher_open_app, (void *)app->name);
}

static void page_gesture_cb(lv_event_t *e)
{
    (void)e;
    lv_indev_t *indev = lv_indev_get_act();
    if (!indev) return;

    lv_dir_t dir = lv_indev_get_gesture_dir(indev);
    if (dir == LV_DIR_LEFT && s_home_ctx.current_page + 1 < s_home_ctx.page_count) {
        lv_async_call(show_page_async, (void *)(intptr_t)(s_home_ctx.current_page + 1));
    } else if (dir == LV_DIR_RIGHT && s_home_ctx.current_page > 0) {
        lv_async_call(show_page_async, (void *)(intptr_t)(s_home_ctx.current_page - 1));
    } else return;

    /* A swipe across an app icon must not also trigger its click. */
    lv_indev_wait_release(indev);
}

static void create_app_grid_button(application_t *app, int col, int row)
{
    lv_obj_t *button = lv_button_create(s_home_ctx.grid);
    lv_obj_set_size(button, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(button, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(button, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(button, 0, LV_PART_MAIN);
    lv_obj_set_flex_flow(button, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(button, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(button, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_row(button, 8, LV_PART_MAIN);
    lv_obj_set_grid_cell(button, LV_GRID_ALIGN_STRETCH, col, 1,
                         LV_GRID_ALIGN_STRETCH, row, 1);
    lv_obj_add_event_cb(button, app_launcher_event_cb, LV_EVENT_CLICKED, app);
    lv_obj_add_event_cb(button, page_gesture_cb, LV_EVENT_GESTURE, NULL);
    lv_obj_clear_flag(button, LV_OBJ_FLAG_GESTURE_BUBBLE);

    lv_obj_t *icon_box = lv_obj_create(button);
    lv_obj_set_size(icon_box, s_home_ctx.icon_size, s_home_ctx.icon_size);
    lv_obj_set_style_bg_opa(icon_box, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(icon_box, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(icon_box, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_radius(icon_box, 6, LV_PART_MAIN);
    lv_obj_set_style_pad_all(icon_box, 0, LV_PART_MAIN);
    lv_obj_clear_flag(icon_box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(icon_box, LV_OBJ_FLAG_CLICKABLE);

    if (app->icon) {
        lv_obj_t *image = lv_image_create(icon_box);
        lv_image_set_src(image, app->icon);
        lv_obj_center(image);
    } else {
        lv_obj_t *fallback = lv_label_create(icon_box);
        lv_label_set_text(fallback, LV_SYMBOL_IMAGE);
        lv_obj_set_style_text_font(fallback, LV_FONT_SMALL, LV_PART_MAIN);
        lv_obj_center(fallback);
    }

    lv_obj_t *label = lv_label_create(button);
    lv_label_set_text(label, app->name);
    lv_obj_set_style_text_font(label, LV_FONT_SMALL, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_width(label, s_home_ctx.icon_size);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
}

static void show_page(int page)
{
    if (page < 0 || page >= s_home_ctx.page_count || page == s_home_ctx.current_page) return;
    s_home_ctx.current_page = page;
    lv_obj_clean(s_home_ctx.grid);

    int visible_index = 0;
    int app_count = app_manager_get_num_apps();
    for (int i = 0; i < app_count; i++) {
        application_t *app = app_manager_get_app(i);
        if (!app || app->hidden) continue;
        if (visible_index >= page * HOME_PAGE_SIZE &&
            visible_index < (page + 1) * HOME_PAGE_SIZE) {
            int slot = visible_index % HOME_PAGE_SIZE;
            create_app_grid_button(app, slot % HOME_COLUMNS, slot / HOME_COLUMNS);
        }
        visible_index++;
        if (visible_index >= HOME_PAGE_SIZE * HOME_MAX_PAGES) break;
    }

    if (visible_index == 0) {
        lv_obj_t *empty = lv_label_create(s_home_ctx.grid);
        lv_label_set_text(empty, "No apps installed");
        lv_obj_set_grid_cell(empty, LV_GRID_ALIGN_CENTER, 0, HOME_COLUMNS,
                             LV_GRID_ALIGN_CENTER, 0, 1);
    }

}

void launcher_home_ui(void)
{
    Page home = lv_page_create("Applications", false, NULL, NULL);
    lv_obj_del(home.header);
    lv_obj_set_style_pad_row(home.container, 0, LV_PART_MAIN);
    lv_obj_clear_flag(home.container, LV_OBJ_FLAG_SCROLLABLE);

    s_home_ctx.grid = lv_obj_create(home.container);
    lv_obj_set_width(s_home_ctx.grid, LV_PCT(100));
    lv_obj_set_flex_grow(s_home_ctx.grid, 1);
    lv_obj_set_style_border_width(s_home_ctx.grid, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_home_ctx.grid, 10, LV_PART_MAIN);
    lv_obj_set_style_pad_row(s_home_ctx.grid, 20, LV_PART_MAIN);
    lv_obj_set_style_pad_column(s_home_ctx.grid, 20, LV_PART_MAIN);
    lv_obj_set_layout(s_home_ctx.grid, LV_LAYOUT_GRID);
    lv_obj_clear_flag(s_home_ctx.grid, LV_OBJ_FLAG_SCROLLABLE);
    /* Keep gestures on empty grid cells here; the second page is mostly empty. */
    lv_obj_clear_flag(s_home_ctx.grid, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(s_home_ctx.grid, page_gesture_cb, LV_EVENT_GESTURE, NULL);
    static const lv_coord_t cols[] = {
        LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST
    };
    static const lv_coord_t rows[] = {
        LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1),
        LV_GRID_TEMPLATE_LAST
    };
    lv_obj_set_grid_dsc_array(s_home_ctx.grid, cols, rows);

    int visible_count = 0;
    int app_count = app_manager_get_num_apps();
    for (int i = 0; i < app_count; i++) {
        application_t *app = app_manager_get_app(i);
        if (app && !app->hidden) visible_count++;
    }
    s_home_ctx.page_count = visible_count > HOME_PAGE_SIZE ? HOME_MAX_PAGES : 1;
    if (visible_count > HOME_PAGE_SIZE * HOME_MAX_PAGES) {
        ESP_LOGW(TAG, "Only first %d of %d visible apps fit on home",
                 HOME_PAGE_SIZE * HOME_MAX_PAGES, visible_count);
    }
    if (s_home_ctx.current_page >= s_home_ctx.page_count) s_home_ctx.current_page = s_home_ctx.page_count - 1;

    lv_scr_load(home.screen);
    lv_obj_update_layout(home.screen);
    lv_coord_t cell_w = (lv_obj_get_width(s_home_ctx.grid) - 20 - 40) / HOME_COLUMNS;
    lv_coord_t cell_h = (lv_obj_get_height(s_home_ctx.grid) - 20 - 60) / HOME_ROWS;
    s_home_ctx.icon_size = cell_w;
    lv_coord_t max_icon_h = cell_h - lv_font_get_line_height(LV_FONT_SMALL) - 8;
    if (s_home_ctx.icon_size > max_icon_h) s_home_ctx.icon_size = max_icon_h;

    int page = s_home_ctx.current_page;
    s_home_ctx.current_page = -1;
    show_page(page);
    ESP_LOGI(TAG, "Home UI ready: %d visible apps, page %d/%d",
             visible_count, page + 1, s_home_ctx.page_count);
}
