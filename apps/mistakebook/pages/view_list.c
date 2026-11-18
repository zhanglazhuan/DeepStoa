#include "../view.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "../app.h"
#include "../controller.h"
#include "../model.h"
#include "widgets/lv_floating_button.h"
#include "widgets/lv_page.h"
#include "theme/lv_theme_hardcore.h"
#include "view_list.h"

LV_IMAGE_DECLARE(refresh);

void mistakebook_view_refresh_list(MistakebookApp *app)
{
    if (!app || !app->view || !app->view->list || !app->model) return;
    lv_obj_t *list = app->view->list;
    lv_obj_clean(list);
    MistakebookModel *model = app->model;
    if (model->load_error || model->read_only) {
        lv_obj_t *notice = lv_label_create(list);
        lv_obj_set_width(notice, LV_PCT(100));
        lv_label_set_long_mode(notice, LV_LABEL_LONG_WRAP);
        lv_label_set_text(notice, model->load_error
            ? "Could not read cards.csv. Check the SD card."
            : "Only the first 32 cards are shown, or the file has unsupported rows. Review changes are disabled to protect the source file.");
    }
    if (model->count == 0) {
        lv_obj_t *empty = lv_label_create(list);
        lv_label_set_text(empty, "No mistake cards found");
        return;
    }
    for (uint32_t i = 0; i < model->count; ++i) {
        const MistakebookCard *card = &model->cards[i];
        lv_obj_t *row = lv_button_create(list);
        lv_obj_set_width(row, LV_PCT(100));
        lv_obj_set_height(row, 48);
        lv_obj_add_event_cb(row, mistakebook_controller_select, LV_EVENT_CLICKED,
                            (void *)(uintptr_t)(i + 1));
        lv_obj_t *label = lv_label_create(row);
        lv_obj_set_width(label, LV_PCT(96));
        lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
        lv_label_set_text_fmt(label, "%u. %s / %s  [%s]", (unsigned)(i + 1),
                              card->subject[0] ? card->subject : "Unknown",
                              card->title[0] ? card->title : "Untitled",
                              card->review_pending ? "Pending" : "Reviewed");
        lv_obj_center(label);
    }
}

static lv_obj_t *build_list_page(void *handle, void *user_data)
{
    (void)user_data;
    MistakebookApp *app = handle;
    Page page = lv_page_create("Mistakebook", false, NULL, NULL);
    lv_obj_set_width(page.container, LV_PCT(100));
    lv_obj_set_style_pad_row(page.container, 8, 0);

    app->view->list = lv_obj_create(page.container);
    lv_obj_set_width(app->view->list, LV_PCT(100));
    lv_obj_set_flex_grow(app->view->list, 1);
    lv_obj_set_flex_flow(app->view->list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(app->view->list, 8, 0);
    lv_obj_set_style_pad_bottom(app->view->list,
                                LV_FLOATING_BUTTON_SIZE + LV_FLOATING_BUTTON_MARGIN, 0);
    lv_floating_button_create_image(page.container, &refresh,
                                    mistakebook_controller_reload, NULL);
    mistakebook_view_refresh_list(app);
    return page.screen;
}

void mistakebook_view_list_init_registry(MistakebookApp *app)
{
    page_navigator_register_page(&app->view->page_nav, MISTAKEBOOK_PAGE_LIST, build_list_page);
}
