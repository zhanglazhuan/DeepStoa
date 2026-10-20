#include <stdio.h>
#include <sys/stat.h>
#include <time.h>

#include <lvgl.h>

#include "lv_page.h"
#include "lv_toast.h"
#include "lv_ui_style_guide.h"

#include "view_trash.h"
#include "../controller.h"
#include "../trash.h"
#include "../view.h"

static void reload_page(AnkiApp *app)
{
    page_navigator_navigate_to(&app->view->page_nav, app, PAGE_TRASH, NULL);
}

static void restore_cb(lv_event_t *event)
{
    AnkiApp *app = lv_event_get_user_data(event);
    uint32_t group_id = (uint32_t)(uintptr_t)lv_obj_get_user_data(
        lv_event_get_current_target(event));
    if (!anki_trash_restore_group(app->model, group_id)) {
        lv_toast_show("Restore failed or filename already exists", 2500);
        return;
    }
    anki_controller_sync(app);
    lv_toast_show("Deck restored", 1800);
    page_navigator_navigate_pop(&app->view->page_nav, app);
}

static void delete_cb(lv_event_t *event)
{
    AnkiApp *app = lv_event_get_user_data(event);
    uint32_t group_id = (uint32_t)(uintptr_t)lv_obj_get_user_data(
        lv_event_get_current_target(event));
    if (!anki_trash_delete_group_permanently(group_id)) {
        lv_toast_show("Permanent deletion failed", 2500);
        return;
    }
    lv_toast_show("Permanently deleted", 1800);
    reload_page(app);
}

static void restore_copy_cb(lv_event_t *event)
{
    AnkiApp *app = lv_event_get_user_data(event);
    uint32_t group_id = (uint32_t)(uintptr_t)lv_obj_get_user_data(
        lv_event_get_current_target(event));
    if (!anki_trash_restore_group_as_copy(app->model, group_id)) {
        lv_toast_show("Restore copy failed; source file may be missing", 2800);
        return;
    }
    anki_controller_sync(app);
    lv_toast_show("Restored with a new filename", 2000);
    page_navigator_navigate_pop(&app->view->page_nav, app);
}

static void add_group_row(lv_obj_t *parent, AnkiApp *app,
                          const AnkiTrashEntry *entry)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(row, 12, 0);
    lv_obj_set_style_pad_gap(row, 10, 0);

    lv_obj_t *name = lv_label_create(row);
    lv_label_set_text(name, entry->deck.name);
    lv_obj_set_width(name, LV_PCT(100));
    lv_label_set_long_mode(name, LV_LABEL_LONG_CLIP);

    char detail[64];
    struct stat trash_stat;
    if (entry->trash_path[0] && stat(entry->trash_path, &trash_stat) != 0) {
        snprintf(detail, sizeof(detail), "Source file missing");
    } else if (entry->deleted_at >= UINT32_C(1704067200)) {
        time_t deleted = (time_t)entry->deleted_at;
        struct tm local;
        localtime_r(&deleted, &local);
        strftime(detail, sizeof(detail), "Deleted %Y-%m-%d · kept 30 days", &local);
    } else {
        snprintf(detail, sizeof(detail), "Kept 30 days after time sync");
    }
    lv_obj_t *description = lv_label_create(row);
    lv_label_set_text(description, detail);

    lv_obj_t *buttons = lv_obj_create(row);
    lv_obj_remove_style_all(buttons);
    lv_obj_set_width(buttons, LV_PCT(100));
    lv_obj_set_height(buttons, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(buttons, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(buttons, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(buttons, 8, 0);

    lv_obj_t *restore = lv_button_create(buttons);
    lv_obj_set_size(restore, LV_PCT(100), 46);
    ui_style_set_btn_primary(restore);
    lv_obj_set_user_data(restore, (void *)(uintptr_t)entry->group_id);
    lv_obj_add_event_cb(restore, restore_cb, LV_EVENT_CLICKED, app);
    lv_obj_t *restore_label = lv_label_create(restore);
    lv_label_set_text(restore_label, "Restore");
    lv_obj_center(restore_label);

    lv_obj_t *restore_copy = lv_button_create(buttons);
    lv_obj_set_size(restore_copy, LV_PCT(100), 46);
    ui_style_set_btn_secondary(restore_copy);
    lv_obj_set_user_data(restore_copy, (void *)(uintptr_t)entry->group_id);
    lv_obj_add_event_cb(restore_copy, restore_copy_cb, LV_EVENT_CLICKED, app);
    lv_obj_t *copy_label = lv_label_create(restore_copy);
    lv_label_set_text(copy_label, "Restore as Copy");
    lv_obj_center(copy_label);

    lv_obj_t *remove = lv_button_create(buttons);
    lv_obj_set_size(remove, LV_PCT(100), 46);
    ui_style_set_btn_secondary(remove);
    lv_obj_set_user_data(remove, (void *)(uintptr_t)entry->group_id);
    lv_obj_add_event_cb(remove, delete_cb, LV_EVENT_CLICKED, app);
    lv_obj_t *remove_label = lv_label_create(remove);
    lv_label_set_text(remove_label, "Delete Permanently");
    lv_obj_center(remove_label);
}

static lv_obj_t *build_trash_page(AnkiApp *app, void *user_data)
{
    (void)user_data;
    Page page = lv_page_create("Recycle Bin", true,
                               page_navigator_navigate_back,
                               &app->view->page_nav);
    lv_obj_set_flex_flow(page.container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(page.container, LV_DIR_VER);
    lv_obj_set_style_pad_row(page.container, 12, 0);

    size_t count = anki_trash_group_count();
    if (count == 0) {
        lv_obj_t *empty = lv_label_create(page.container);
        lv_label_set_text(empty, "Recycle Bin is empty");
        lv_obj_center(empty);
        return page.screen;
    }

    for (size_t i = 0; i < count; i++) {
        AnkiTrashEntry entry;
        if (anki_trash_get_group(i, &entry)) add_group_row(page.container, app, &entry);
    }
    return page.screen;
}

void anki_view_trash_init_registry(struct AnkiApp *app)
{
    PAGE_REGISTE(app, PAGE_TRASH, build_trash_page);
}
