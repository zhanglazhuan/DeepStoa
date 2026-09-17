/**
 * @file lv_tab.h
 * @brief Lightweight custom tab widget — replaces lv_tabview for I1 e-ink.
 *
 * Flat structure, no theme dependency, styles applied directly.
 *
 *   root (flex column)
 *   ├── tab_bar (flex row, pad_gap=0)
 *   │   ├── button[0] + label
 *   │   └── ...
 *   └── content (flex_grow=1)
 *       ├── page[0] ← lv_tab_add_page()
 *       └── ...
 */

#ifndef LV_TAB_H
#define LV_TAB_H

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Types ─────────────────────────────────────────────────────────── */

typedef struct _lv_tab_t lv_tab_t;

/** Called when active tab changes. */
typedef void (*lv_tab_changed_cb_t)(lv_tab_t *tab, uint32_t idx, void *user_data);

/* ── Lifecycle ─────────────────────────────────────────────────────── */

/**
 * @brief Create a custom tab widget.
 *
 * Pages are created empty. Use lv_tab_add_page() to get each page object
 * and populate it. After all pages are ready, call lv_tab_finalize().
 *
 * @param parent      Parent object.
 * @param names       Tab button label strings (static, not copied).
 * @param count       Number of tabs.
 * @param bar_height  Tab bar height in pixels.
 */
lv_tab_t *lv_tab_create(lv_obj_t *parent, const char **names,
                        uint32_t count, lv_coord_t bar_height);

/**
 * @brief Add a content page. Returns the page object — populate it directly.
 *
 * Must be called exactly @em count times after lv_tab_create().
 * Pages are created hidden except page 0.
 */
lv_obj_t *lv_tab_add_page(lv_tab_t *tab);

/**
 * @brief Finalize after all pages added — sets initial active tab state.
 */
void lv_tab_finalize(lv_tab_t *tab);

/* ── Accessors ─────────────────────────────────────────────────────── */

lv_obj_t *lv_tab_get_content(lv_tab_t *tab);
lv_obj_t *lv_tab_get_tab_bar(lv_tab_t *tab);
lv_obj_t *lv_tab_get_root(lv_tab_t *tab);

/* ── Control ───────────────────────────────────────────────────────── */

void     lv_tab_set_active(lv_tab_t *tab, uint32_t idx);
uint32_t lv_tab_get_active(lv_tab_t *tab);
uint32_t lv_tab_get_count(lv_tab_t *tab);

/**
 * @brief Register a callback that fires when the active tab changes.
 *
 * The callback receives (tab, new_index, user_data).
 */
void lv_tab_set_changed_cb(lv_tab_t *tab, lv_tab_changed_cb_t cb, void *user_data);

#ifdef __cplusplus
}
#endif

#endif /* LV_TAB_H */
