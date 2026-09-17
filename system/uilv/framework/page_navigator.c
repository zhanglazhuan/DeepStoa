#include <stdio.h>
#include <stdlib.h>
#include "page_navigator.h"
#include "esp_log.h"
#include "log_system.h"

static const char *TAG = "nav";

static void free_page_context_async(void *context)
{
	free(context);
}

static void release_page_context_on_delete(lv_event_t *event)
{
	void *context = lv_event_get_user_data(event);
	if (!context) return;

	/* A screen receives DELETE before its children.  Defer the free so child
	 * DELETE callbacks can still read their page context during teardown. */
	if (lv_async_call(free_page_context_async, context) != LV_RESULT_OK) {
		ESP_LOGE(TAG, "failed to schedule page context release");
	}
}

static void release_old_page_context(lv_obj_t *old_screen, void *old_context,
				     void *new_context)
{
	if (!old_context || old_context == new_context) return;

	if (old_screen) {
		lv_obj_add_event_cb(old_screen, release_page_context_on_delete,
				    LV_EVENT_DELETE, old_context);
	}
	else {
		free(old_context);
	}
}

static page_builder_cb_t find_builder(page_navigator_t *nav, int page_id)
{
	for (int i = 0; i < nav->registry_count; i++) {
		if (nav->registry[i].page_id == page_id) {
			return nav->registry[i].builder;
		}
	}
	return NULL;
}

void page_navigator_init(page_navigator_t *nav, page_navigator_page_t *registry,
			 int max_pages, app_handle_t app)
{
	nav->registry = registry;
	nav->registry_size = max_pages;
	nav->registry_count = 0;
	nav->stack_top = -1;
	nav->current_page = 0;   /* PAGE_NONE */
	nav->nav_ctx = NULL;
	nav->app_handler = app;
}

int page_navigator_register_page(page_navigator_t *nav, int page_id, page_builder_cb_t builder)
{
	if (nav->registry_count >= nav->registry_size) {
		ESP_LOGE(TAG, "registry full");
		return -1;
	}
	nav->registry[nav->registry_count].page_id = page_id;
	nav->registry[nav->registry_count].builder = builder;
	nav->registry_count++;
	return 0;
}

void page_navigator_navigate_to(page_navigator_t *nav, app_handle_t app, int page_id, void *user_data)
{
	page_builder_cb_t builder = find_builder(nav, page_id);
	if (!builder) {
		ESP_LOGE(TAG, "no builder for page %d", page_id);
		return;
	}

	/* Build the new screen while the old one is still active so act_scr
	 * is never dangling.  After loading the new screen, use async delete
	 * for the old screen — lv_obj_delete_async defers the actual deletion
	 * to a later timer callback, outside the current event handler.
	 * Calling lv_obj_delete synchronously from within an LVGL event
	 * handler (e.g. a button click on the old screen) corrupts internal
	 * LVGL state because the event source is freed before the event
	 * dispatch completes. */
	lv_obj_t *old_screen = lv_screen_active();
	void *old_context = nav->nav_ctx;

	/* user_data is a borrowed navigation payload, not an owned page context.
	 * The builder receives it explicitly and publishes its new context through
	 * nav_ctx.  Keeping these channels separate prevents values such as enum
	 * payloads (0x1/0x2) and model pointers from reaching free(). */
	nav->nav_ctx = NULL;
	lv_obj_t *new_screen = builder(app, user_data);

	if (new_screen) {
		lv_screen_load(new_screen);
		release_old_page_context(old_screen, old_context, nav->nav_ctx);
		if (old_screen) lv_obj_delete_async(old_screen);
		/* current_page 必须对所有调用方都成立，不能只靠 PAGE_NAVIGATE_TO 宏在
		 * 外面补写：裸调用本函数进入的页面，若之后再用宏跳转，会把过期的页号
		 * 压进返回栈。宏是先 push 旧 current_page 再调本函数，顺序上不冲突。 */
		nav->current_page = page_id;
		ESP_LOGI(TAG, "navigated to page %d", page_id);
		log_page_nav("push", "page");
	} else {
		nav->nav_ctx = old_context;
		ESP_LOGE(TAG, "builder returned NULL for page %d", page_id);
	}
}

bool page_navigator_navigate_pop(page_navigator_t *nav, app_handle_t app)
{
	if (!nav) return false;

	if (nav->stack_top < 0) {
		ESP_LOGI(TAG, "already at root, cannot go back");
		return false;
	}

	int prev_id = nav->stack[nav->stack_top];
	nav->stack_top--;

	page_builder_cb_t builder = find_builder(nav, prev_id);
	if (!builder) {
		ESP_LOGE(TAG, "no builder for page %d", prev_id);
		return false;
	}

	/* Build new screen before deleting the old one, and use async
	 * delete — same rationale as page_navigator_navigate_to. */
	lv_obj_t *old_screen = lv_screen_active();
	void *old_context = nav->nav_ctx;
	nav->nav_ctx = NULL;
	lv_obj_t *new_screen = builder(app, NULL);
	if (new_screen) {
		lv_screen_load(new_screen);
		release_old_page_context(old_screen, old_context, nav->nav_ctx);
		if (old_screen) lv_obj_delete_async(old_screen);
		nav->current_page = prev_id;
		ESP_LOGI(TAG, "popped to page %d", prev_id);
		log_page_nav("pop", "page");
		return true;
	}
	nav->nav_ctx = old_context;
	return false;
}

void page_navigator_push(page_navigator_t *nav, int page_id)
{
	if (!nav || page_id <= 0) return;
	if (nav->stack_top >= 9) return;
	nav->stack[++nav->stack_top] = page_id;
}

void page_navigator_navigate_back(lv_event_t *e)
{
	page_navigator_t *nav = (page_navigator_t *)lv_event_get_user_data(e);
	if (nav) {
		page_navigator_navigate_pop(nav, nav->app_handler);
	}
}

void page_navigator_deinit(page_navigator_t *nav)
{
	if (!nav) return;
	nav->registry = NULL;
	nav->registry_size = 0;
	nav->registry_count = 0;
	nav->stack_top = -1;
	nav->app_handler = NULL;
	/* nav_ctx is opaque: its owner is the page builder/event context.
	 * Never guess its allocator here (it may be malloc, lv_malloc, or
	 * caller-owned user data). */
	nav->nav_ctx = NULL;
}
