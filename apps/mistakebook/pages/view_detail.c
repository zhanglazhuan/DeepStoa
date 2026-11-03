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
#include "view_detail.h"

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

void mistakebook_view_refresh_detail(MistakebookApp *app)
{
    if (!app || !app->view) return;
    page_navigator_navigate_to(&app->view->page_nav, app,
                               MISTAKEBOOK_PAGE_DETAIL, NULL);
}

void mistakebook_view_detail_init_registry(MistakebookApp *app)
{
    page_navigator_register_page(&app->view->page_nav, MISTAKEBOOK_PAGE_DETAIL, build_detail_page);
}
