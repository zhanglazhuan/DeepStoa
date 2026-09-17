/**
 * @file lv_tab.c
 * @brief Lightweight custom tab widget — no lv_tabview, no theme dependency.
 */

#include "lv_tab.h"
#include <string.h>
#include <stdlib.h>

#define C_BLACK lv_color_black()
#define C_WHITE lv_color_white()

struct _lv_tab_t {
    lv_obj_t   *root;
    lv_obj_t   *tab_bar;
    lv_obj_t   *content;
    uint32_t    tab_count;
    uint32_t    active;
    lv_tab_changed_cb_t changed_cb;
    void       *cb_user_data;
};

/* ── Internal: style helper ────────────────────────────────────────── */

static void tab_apply_checked(lv_obj_t *btn, lv_obj_t *label)
{
    /* tab_bar provides top/left/right border, button provides bottom */
    lv_obj_set_style_bg_color(btn, C_BLACK, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, C_WHITE, LV_PART_MAIN);
    lv_obj_set_style_border_width(btn, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(btn, C_BLACK, LV_PART_MAIN);
    lv_obj_set_style_border_side(btn, LV_BORDER_SIDE_BOTTOM, LV_PART_MAIN);
}

static void tab_apply_unchecked(lv_obj_t *btn, lv_obj_t *label)
{
    lv_obj_set_style_bg_color(btn, C_WHITE, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, C_BLACK, LV_PART_MAIN);
    lv_obj_set_style_border_width(btn, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(btn, C_BLACK, LV_PART_MAIN);
    lv_obj_set_style_border_side(btn, LV_BORDER_SIDE_BOTTOM, LV_PART_MAIN);
}

/* ── Internal: button click ────────────────────────────────────────── */

static void tab_btn_event_cb(lv_event_t *e)
{
    lv_obj_t *clicked_btn = lv_event_get_target(e);
    lv_obj_t *tab_bar     = lv_obj_get_parent(clicked_btn);
    lv_tab_t *tab         = lv_event_get_user_data(e);
    uint32_t  btn_cnt     = lv_obj_get_child_count(tab_bar);
    uint32_t  clicked     = lv_obj_get_index(clicked_btn);

    if (clicked == tab->active) return;  /* no-op */

    /* Update button styles */
    for (uint32_t i = 0; i < btn_cnt; i++) {
        lv_obj_t *btn   = lv_obj_get_child(tab_bar, i);
        lv_obj_t *label = lv_obj_get_child(btn, 0);
        if (i == clicked)
            tab_apply_checked(btn, label);
        else
            tab_apply_unchecked(btn, label);
    }

    /* Show/hide pages */
    uint32_t pc = lv_obj_get_child_count(tab->content);
    for (uint32_t i = 0; i < pc; i++) {
        lv_obj_t *p = lv_obj_get_child(tab->content, i);
        if (i == clicked) lv_obj_remove_flag(p, LV_OBJ_FLAG_HIDDEN);
        else              lv_obj_add_flag(p, LV_OBJ_FLAG_HIDDEN);
    }

    tab->active = clicked;

    if (tab->changed_cb)
        tab->changed_cb(tab, clicked, tab->cb_user_data);
}

/* ── Public API ────────────────────────────────────────────────────── */

lv_tab_t *lv_tab_create(lv_obj_t *parent, const char **names,
                        uint32_t count, lv_coord_t bar_height)
{
    if (!parent || !names || count == 0) return NULL;

    lv_tab_t *tab = calloc(1, sizeof(lv_tab_t));
    if (!tab) return NULL;
    tab->tab_count = count;

    /* Root */
    tab->root = lv_obj_create(parent);
    lv_obj_set_size(tab->root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_pad_all(tab->root, 0, 0);
    lv_obj_set_style_border_width(tab->root, 0, 0);
    lv_obj_set_style_bg_color(tab->root, C_WHITE, 0);
    lv_obj_set_flex_flow(tab->root, LV_FLEX_FLOW_COLUMN);

    /* Tab bar */
    tab->tab_bar = lv_obj_create(tab->root);
    lv_obj_set_size(tab->tab_bar, LV_PCT(100), bar_height);
    lv_obj_set_style_pad_all(tab->tab_bar, 0, 0);
    lv_obj_set_style_pad_gap(tab->tab_bar, 0, 0);
    lv_obj_set_style_margin_top(tab->tab_bar, 16, 0);  /* push down from screen top */
    lv_obj_set_style_bg_color(tab->tab_bar, C_WHITE, 0);
    /* Full tab bar border: top+bottom+left+right */
    lv_obj_set_style_border_width(tab->tab_bar, 2, 0);
    lv_obj_set_style_border_color(tab->tab_bar, C_BLACK, 0);
    lv_obj_set_style_border_side(tab->tab_bar, LV_BORDER_SIDE_TOP | LV_BORDER_SIDE_LEFT | LV_BORDER_SIDE_RIGHT, 0);
    lv_obj_set_flex_flow(tab->tab_bar, LV_FLEX_FLOW_ROW);

    /* Tab buttons */
    for (uint32_t i = 0; i < count; i++) {
        lv_obj_t *btn = lv_button_create(tab->tab_bar);
        lv_obj_remove_style_all(btn);  /* kill theme styles */
        lv_obj_set_height(btn, LV_PCT(100));
        lv_obj_set_flex_grow(btn, 1);
        lv_obj_set_style_radius(btn, 0, 0);
        lv_obj_set_style_shadow_width(btn, 0, 0);
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_color(btn, C_BLACK, LV_PART_MAIN);

        lv_obj_t *label = lv_label_create(btn);
        lv_obj_remove_style_all(label);  /* kill theme text-color */
        lv_label_set_text(label, names[i]);
        lv_obj_center(label);

        lv_obj_add_event_cb(btn, tab_btn_event_cb, LV_EVENT_CLICKED, tab);

        if (i == 0) tab_apply_checked(btn, label);
        else        tab_apply_unchecked(btn, label);
    }

    /* Content area */
    tab->content = lv_obj_create(tab->root);
    lv_obj_set_size(tab->content, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_grow(tab->content, 1);
    lv_obj_set_style_pad_all(tab->content, 0, 0);
    lv_obj_set_style_border_width(tab->content, 0, 0);
    lv_obj_set_style_bg_color(tab->content, C_WHITE, 0);
    lv_obj_clear_flag(tab->content, LV_OBJ_FLAG_SCROLLABLE);

    return tab;
}

lv_obj_t *lv_tab_add_page(lv_tab_t *tab)
{
    if (!tab) return NULL;

    lv_obj_t *page = lv_obj_create(tab->content);
    lv_obj_set_size(page, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_border_width(page, 0, 0);
    lv_obj_set_style_bg_color(page, C_WHITE, 0);

    /* All pages start hidden; finalize() or first click shows page 0 */
    lv_obj_add_flag(page, LV_OBJ_FLAG_HIDDEN);

    return page;
}

void lv_tab_finalize(lv_tab_t *tab)
{
    if (!tab || tab->tab_count == 0) return;

    /* Show page 0, highlight button 0 */
    lv_obj_t *p0 = lv_obj_get_child(tab->content, 0);
    if (p0) lv_obj_remove_flag(p0, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *b0 = lv_obj_get_child(tab->tab_bar, 0);
    lv_obj_t *l0 = b0 ? lv_obj_get_child(b0, 0) : NULL;
    if (b0 && l0) tab_apply_checked(b0, l0);

    tab->active = 0;
}

/* ── Accessors ─────────────────────────────────────────────────────── */

lv_obj_t *lv_tab_get_content(lv_tab_t *tab) { return tab ? tab->content : NULL; }
lv_obj_t *lv_tab_get_tab_bar(lv_tab_t *tab) { return tab ? tab->tab_bar : NULL; }
lv_obj_t *lv_tab_get_root(lv_tab_t *tab)    { return tab ? tab->root : NULL; }

void lv_tab_set_active(lv_tab_t *tab, uint32_t idx)
{
    if (!tab || idx >= tab->tab_count || idx == tab->active) return;

    lv_obj_t *btn = lv_obj_get_child(tab->tab_bar, idx);
    if (!btn) return;

    /* Simulate a click to trigger the full state update in the callback.
     * 不能在这里预先写 tab->active = idx：tab_btn_event_cb 开头就是
     * `if (clicked == tab->active) return;`，预写会让它立刻 early-return，
     * 于是页面不切换、按钮样式不更新、changed_cb 也不触发。
     * active 由 tab_btn_event_cb 自己在完成切换后写入。 */
    lv_obj_send_event(btn, LV_EVENT_CLICKED, NULL);
}

uint32_t lv_tab_get_active(lv_tab_t *tab) { return tab ? tab->active : 0; }
uint32_t lv_tab_get_count(lv_tab_t *tab)  { return tab ? tab->tab_count : 0; }

void lv_tab_set_changed_cb(lv_tab_t *tab, lv_tab_changed_cb_t cb, void *user_data)
{
    if (!tab) return;
    tab->changed_cb   = cb;
    tab->cb_user_data = user_data;
}
