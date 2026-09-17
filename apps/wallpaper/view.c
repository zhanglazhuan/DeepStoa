#include "esp_log.h"
#include "view.h"

static const char *TAG = "wallpaper_view";

void wallpaper_view_init(WallpaperView* view) {
    if (!view) return;
    view->overlay = NULL;
    view->img_obj = NULL;
    view->label_obj = NULL;
}

void wallpaper_view_show(WallpaperView* view, lv_event_cb_t wake_up_cb, void* user_data) {
    if (view->overlay) return; // 已经显示

    // 在系统层 (最高层) 创建一个全屏容器
    view->overlay = lv_obj_create(lv_layer_top());
    lv_obj_set_size(view->overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(view->overlay, lv_color_black(), 0);
    lv_obj_set_style_border_width(view->overlay, 0, 0);
    lv_obj_set_style_radius(view->overlay, 0, 0);
    
    // 拦截所有触摸事件，防止点击穿透到底层应用
    lv_obj_add_flag(view->overlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(view->overlay, wake_up_cb, LV_EVENT_PRESSED, user_data);
}

void wallpaper_view_hide(WallpaperView* view) {
    if (view->overlay) {
        lv_obj_del(view->overlay);
        view->overlay = NULL;
        view->img_obj = NULL;
        view->label_obj = NULL;
    }
}

void wallpaper_view_set_image(WallpaperView* view, const char* img_path) {
    if (!view->overlay || !img_path) return;

    // 图片模式：黑底
    lv_obj_set_style_bg_color(view->overlay, lv_color_black(), 0);

    if (view->label_obj) {
        lv_obj_del(view->label_obj);
        view->label_obj = NULL;
    }

    if (!view->img_obj) {
        view->img_obj = lv_image_create(view->overlay);
        lv_obj_center(view->img_obj);
    }
    lv_image_set_src(view->img_obj, img_path);
}

void wallpaper_view_set_text(WallpaperView* view, const char* text) {
    if (!view->overlay || !text) return;

    // 文本模式：白底黑字
    lv_obj_set_style_bg_color(view->overlay, lv_color_white(), 0);

    if (view->img_obj) {
        lv_obj_del(view->img_obj);
        view->img_obj = NULL;
    }

    if (!view->label_obj) {
        view->label_obj = lv_label_create(view->overlay);
        lv_obj_set_style_text_font(view->label_obj, LV_FONT_NORMAL, 0);
        lv_obj_set_style_text_color(view->label_obj, lv_color_black(), 0);
        lv_label_set_long_mode(view->label_obj, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(view->label_obj, LV_PCT(80)); // 留出边缘页边距
        lv_obj_set_style_text_align(view->label_obj, LV_TEXT_ALIGN_CENTER, 0);
    }
    
    lv_label_set_text(view->label_obj, text);
    lv_obj_center(view->label_obj);
}