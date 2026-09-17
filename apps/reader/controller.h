#ifndef READER_CONTROLLER_H
#define READER_CONTROLLER_H

#include <lvgl.h>
#include "app.h"
#include "model.h"
#include "view.h"

typedef struct ReaderController{
    ReaderModel *model;
    ReaderView *view;
} ReaderController;

/* Lifecycle */
void reader_controller_init(ReaderApp *app);
void reader_controller_deinit(ReaderApp *app);

/* Library page */
void reader_controller_on_book_clicked(lv_event_t *e);

/* Reading page */
void reader_controller_on_read_next_page(lv_event_t *e);
void reader_controller_on_read_prev_page(lv_event_t *e);
void reader_controller_on_read_slider_changed(lv_event_t *e);
void reader_controller_on_font_toggle_clicked(lv_event_t *e);
void reader_controller_on_back_to_library(lv_event_t *e);

#endif // READER_CONTROLLER_H
