#include "view.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include "app.h"
#include "controller.h"
#include "model.h"
#include "widgets/lv_floating_button.h"
#include "widgets/lv_page.h"
#include "theme/lv_theme_hardcore.h"

LV_IMAGE_DECLARE(refresh);

static lv_obj_t *make_button(lv_obj_t *parent, const char *text,
                             lv_event_cb_t callback, void *user_data)
{
    lv_obj_t *button = lv_button_create(parent);
    lv_obj_set_height(button, 48);
    lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, user_data);
    lv_obj_t *label = lv_label_create(button);
    lv_label_set_text(label, text);
    lv_obj_center(label);
    return button;
}

static void add_field(lv_obj_t *parent, const char *name, const char *value)
{
    lv_obj_t *heading = lv_label_create(parent);
    lv_label_set_text(heading, name);
    lv_obj_set_style_text_font(heading, LV_FONT_NORMAL, 0);
    lv_obj_t *body = lv_label_create(parent);
    lv_obj_set_width(body, LV_PCT(100));
    lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);
    lv_label_set_text(body, value && value[0] ? value : "(not provided)");
    lv_obj_set_style_text_font(body, LV_FONT_SMALL, 0);
    lv_obj_set_style_pad_bottom(body, 12, 0);
}

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

static lv_obj_t *build_detail_page(void *handle, void *user_data)
{
    (void)user_data;
    MistakebookApp *app = handle;
    Page page = lv_page_create("Mistakebook", true,
                               page_navigator_navigate_back, &app->view->page_nav);
    MistakebookModel *model = app->model;
    if (!model || model->selected >= model->count) return page.screen;
    const MistakebookCard *card = &model->cards[model->selected];
    lv_obj_set_style_pad_row(page.container, 4, 0);

    lv_obj_t *scroll = lv_obj_create(page.container);
    lv_obj_set_width(scroll, LV_PCT(100));
    lv_obj_set_flex_grow(scroll, 1);
    lv_obj_set_flex_flow(scroll, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(scroll, 4, 0);

    lv_obj_t *summary = lv_label_create(scroll);
    lv_obj_set_width(summary, LV_PCT(100));
    lv_label_set_long_mode(summary, LV_LABEL_LONG_WRAP);
    lv_label_set_text_fmt(summary, "%u/%u  [%s] %s   %s",
                          (unsigned)(model->selected + 1), (unsigned)model->count,
                          card->subject[0] ? card->subject : "Unknown",
                          card->title[0] ? card->title : "Untitled",
                          card->review_pending ? "Pending" : "Reviewed");
    add_field(scroll, "Question", card->question);
    add_field(scroll, "Your answer", card->student_answer);
    add_field(scroll, "Correct answer", card->correct_answer);
    add_field(scroll, "Knowledge", card->knowledge_point);
    add_field(scroll, "Error type", card->error_type);
    add_field(scroll, "Explanation", card->explanation);

    app->view->detail_status = lv_label_create(page.container);
    lv_obj_set_width(app->view->detail_status, LV_PCT(100));
    lv_label_set_text(app->view->detail_status,
                      model->read_only ? "Review changes disabled: source file cannot be safely rewritten" : "");

    lv_obj_t *actions = lv_obj_create(page.container);
    lv_obj_set_width(actions, LV_PCT(100));
    lv_obj_set_height(actions, 64);
    lv_obj_set_style_border_width(actions, 0, 0);
    lv_obj_set_flex_flow(actions, LV_FLEX_FLOW_ROW);
    make_button(actions, "Previous", mistakebook_controller_previous, NULL);
    make_button(actions, "Next", mistakebook_controller_next, NULL);
    if (card->review_pending) {
        make_button(actions, "Mark reviewed", mistakebook_controller_mark_reviewed, NULL);
    }
    return page.screen;
}

bool mistakebook_view_init(MistakebookApp *app)
{
    app->view = calloc(1, sizeof(MistakebookView));
    if (!app->view) return false;
    page_navigator_page_t *registry = calloc(2, sizeof(*registry));
    if (!registry) {
        free(app->view);
        app->view = NULL;
        return false;
    }
    page_navigator_init(&app->view->page_nav, registry, 2, app);
    page_navigator_register_page(&app->view->page_nav, MISTAKEBOOK_PAGE_LIST, build_list_page);
    page_navigator_register_page(&app->view->page_nav, MISTAKEBOOK_PAGE_DETAIL, build_detail_page);
    return true;
}

void mistakebook_view_deinit(MistakebookApp *app)
{
    if (!app || !app->view) return;
    free(app->view->page_nav.registry);
    page_navigator_deinit(&app->view->page_nav);
    free(app->view);
    app->view = NULL;
}

void mistakebook_view_refresh_detail(MistakebookApp *app)
{
    if (!app || !app->view) return;
    page_navigator_navigate_to(&app->view->page_nav, app,
                               MISTAKEBOOK_PAGE_DETAIL, NULL);
}
