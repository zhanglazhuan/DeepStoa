#include "esp_log.h"

#include "lv_tab.h"
#include "lv_epd_region.h"
#include "../modules/controller_timer.h"
#include "view_home.h"
#include "view_settings.h"
#include "view_about.h"            // 引入 About 页面
#include "controller_settings.h"   // 引入 Clear Done 逻辑

#include "../view.h"
#include "../controller.h"
#include "../app.h"
#include "../modules/view_todo.h"
#include "../modules/view_timer.h"
#include "../modules/view_done.h"

static const char *TAG = "todolist_view_home";

// 专门用来修复 LVGL 丢失事件 Bug 的辅助函数

// --- 底部弹窗的相关回调 ---

// 选项被点击后，关闭底部弹窗的包装回调
static void close_sheet_wrapper_cb(lv_event_t * e) {
    lv_bottom_sheet_t * sheet = lv_event_get_user_data(e);
    lv_bottom_sheet_close(sheet);
}

// 包装 Settings 跳转回调
static void open_settings_wrapper_cb(lv_event_t * e) {
    (void)e;
    PAGE_NAVIGATE_TO(&g_todolist_app, PAGE_SETTINGS, NULL);
}

// 包装 About 跳转回调
static void open_about_wrapper_cb(lv_event_t * e) {
    (void)e;
    PAGE_NAVIGATE_TO(&g_todolist_app, PAGE_ABOUT, NULL);
}

// 点击右上角菜单按钮，呼出 Bottom Sheet
static void open_menu_sheet_event_cb(lv_event_t * e) {
    TodoListApp * app = lv_event_get_user_data(e);
    
    // 1. 创建底部弹窗 (高度 272 足够放下 4 个 60px 的按钮和一些内边距)
    lv_bottom_sheet_t* sheet = lv_bottom_sheet_create(NULL);

    lv_obj_t * content = lv_bottom_sheet_get_content(sheet);
    lv_obj_set_style_pad_all(content, 0, LV_PART_MAIN);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    
    // 2. 准备菜单数据和对应的回调函数
    const char* nav_labels[] = {"Settings", "Clear done list", "About", "Cancel"};
    void (*callbacks[])(lv_event_t *) = {
        open_settings_wrapper_cb, // Settings
        settings_clear_done_cb,   // Clear
        open_about_wrapper_cb,    // About
        NULL                      // Cancel (不需要动作，只关弹窗)
    };
    
    // 3. 循环创建 4 个列表按钮
    for(int i = 0; i < 4; i++) {
        lv_obj_t *btn = lv_btn_create(content);
        lv_obj_set_width(btn, LV_PCT(100)); // 宽度 100%
        lv_obj_set_height(btn, EPOS_BOTTOM_BUTTON_HEIGHT);         // 设定固定高度，增加手指点击热区
        lv_obj_set_style_bg_color(btn, lv_color_white(), 0);
        lv_obj_set_style_text_color(btn, lv_color_black(), 0);
        
        // 样式设计：无圆角，去除默认边框
        lv_obj_set_style_radius(btn, 0, 0);
        lv_obj_set_style_border_width(btn, 0, 0);
        
        // 添加分隔线：给前三个按钮底部加一条黑线
        if (i < 3) {
            lv_obj_set_style_border_width(btn, 1, 0);
            lv_obj_set_style_border_side(btn, LV_BORDER_SIDE_BOTTOM, 0);
            lv_obj_set_style_border_color(btn, lv_color_black(), 0);
        }
        
        // 按钮内的文字标签居中
        lv_obj_t *lbl = lv_label_create(btn);
        lv_label_set_text(lbl, nav_labels[i]);
        lv_obj_center(lbl); 
        
        // --- 绑定双重事件 ---
        // 事件 1：执行对应的功能回调
        if (callbacks[i] != NULL) {
            lv_obj_add_event_cb(btn, callbacks[i], LV_EVENT_CLICKED, app);
        } 

        // 事件 2：功能执行完后，顺手把 Bottom Sheet 关掉
        lv_obj_add_event_cb(btn, close_sheet_wrapper_cb, LV_EVENT_CLICKED, sheet);
    }
}

/* Home 页销毁时退出墨水屏局部刷新模式 —— 窗口钉在 Timer 页的倒计时矩形上，
 * 带着它离开页面，下一页的内容就推不到面板上。
 * 这里不碰 nav_ctx：DELETE 触发时它已经指向新页面的上下文了。 */
static void home_page_delete_cb(lv_event_t * e) {
    (void)e;
    if (g_todolist_app.controller) controller_timer_pause_polling(g_todolist_app.controller);
    if (epd_region_is_active()) epd_region_end();
}

static lv_obj_t* build_home_page(TodoListApp* app) {
    Page page = lv_page_create("TodoList", false, page_navigator_navigate_back, &app->view->page_nav);

    /* 用 Page 暴露的槽位，别再靠 lv_obj_get_child(header, 2) 这种魔法索引 ——
     * lv_page 的 header 结构一变就会静默错位 */
    lv_obj_t* right_slot = page.header_right;
    lv_obj_set_style_margin_right(right_slot, 8, 0); /* 菜单按钮右边距增加 8px */
    lv_obj_t* right_icon = lv_label_create(right_slot);
    lv_label_set_text(right_icon, LV_SYMBOL_BARS); // 添加菜单图标
    lv_obj_center(right_icon); // 在预留的 40x40 槽位中居中图标
    lv_obj_add_flag(right_icon, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(right_icon, 10);
    
    // 【核心修改点】：将直接跳转设置改为呼出 Bottom Sheet
    lv_obj_add_event_cb(right_icon, open_menu_sheet_event_cb, LV_EVENT_CLICKED, app);
    
    static const char *tab_names[] = {"Todo", "Timer", "Done"};
    lv_tab_t *tab = lv_tab_create(page.container, tab_names, 3, 40);
    lv_obj_set_style_radius(lv_tab_get_tab_bar(tab), 0, 0);  /* 直角 tabbar，不要倒角 */
    lv_obj_t *tab_root = lv_tab_get_root(tab);
    lv_obj_set_flex_grow(tab_root, 1);
    lv_tab_set_changed_cb(tab, todolist_controller_on_tab_changed, app);

    /* 不要在这里 free(nav_ctx)：page_navigator 进 builder 前已经把它置 NULL，
     * 旧上下文由它在旧 screen 的 DELETE 回调里释放。这里再 free 一次就是二次释放。 */
    TodoListViewHomeCtx* home_ctx = malloc(sizeof(TodoListViewHomeCtx));
    if (!home_ctx) {
        ESP_LOGE(TAG, "home ctx alloc failed");
        return page.screen;
    }
    memset(home_ctx, 0, sizeof(TodoListViewHomeCtx));
    app->view->page_nav.nav_ctx = home_ctx;

    lv_obj_add_event_cb(page.screen, home_page_delete_cb, LV_EVENT_DELETE, NULL);

    home_ctx->tab = tab;
    home_ctx->tab_todo = lv_tab_add_page(tab);
    home_ctx->tab_timer = lv_tab_add_page(tab);
    home_ctx->tab_done = lv_tab_add_page(tab);

    lv_tab_finalize(tab);

    /* lv_tab_finalize() only shows page 0 and highlights button 0 — it does
     * NOT fire the changed callback. Build the initial tab (todo + its
     * floating add button) explicitly, matching the old lv_tabview flow that
     * sent LV_EVENT_VALUE_CHANGED here. */
    todolist_controller_active_tab(0, false);

    ESP_LOGI(TAG, "[view] view_create_main done\n");

    return page.screen;
}

void todolist_view_home_init_registry(struct TodoListApp* app) {
    PAGE_REGISTE(app, PAGE_HOME, build_home_page);
}