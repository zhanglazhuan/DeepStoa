#include "esp_log.h"
#include "lv_epd_region.h"
#include "display_control.h"

static const char *TAG = "lv_epd_region";

void epd_region_flush_obj(lv_obj_t *obj);   /* 前置声明：基准帧回调里要用 */

static bool      s_active;
static lv_obj_t *s_screen;
static lv_obj_t *s_focus;

/* 基准帧：整屏推一次并让驱动同步差分基准，之后才能只推窗口。
 * 放在 lv_async_call 里执行，避免在事件回调 / 页面 builder 里重入
 * lv_refr_now()（那会嵌套进 LVGL 自己的刷新流程）。 */
static void begin_async_cb(void *arg)
{
    lv_obj_t *screen = arg;

    /* 页面可能在异步回调到来之前就被销毁了 */
    if (!screen || screen != s_screen || screen != lv_screen_active()) {
        ESP_LOGD(TAG, "begin skipped: screen changed");
        return;
    }

    epd_display_set_flush_mode_next_frame(EPD_FLUSH_MODE_PARTIAL_WIN);
    epd_display_set_refresh_area(false, NULL);   /* 基准帧不受窗口限制 */
    lv_obj_invalidate(screen);
    lv_refr_now(NULL);

    s_active = true;

    /* 基准帧已经把整页打到面板了，现在可以把窗口收到目标对象上 */
    if (s_focus) epd_region_flush_obj(s_focus);

    ESP_LOGI(TAG, "partial-window mode active");
}

void epd_region_begin(lv_obj_t *screen)
{
    if (!screen) return;
    s_active = false;
    s_screen = screen;
    s_focus  = NULL;
    lv_async_call(begin_async_cb, screen);
}

void epd_region_begin_focus(lv_obj_t *screen, lv_obj_t *focus_obj)
{
    if (!screen) return;
    s_active = false;
    s_screen = screen;
    s_focus  = focus_obj;
    lv_async_call(begin_async_cb, screen);
}

void epd_region_flush_area(const lv_area_t *area)
{
    if (!area) return;

    if (!s_active) {
        /* 基准帧还没打完：退化成普通刷新，不设窗口 */
        lv_obj_invalidate(lv_screen_active());
        return;
    }
    epd_display_set_refresh_area(true, area);
}

void epd_region_flush_obj(lv_obj_t *obj)
{
    if (!obj) return;

    /* 先把布局算准，否则拿到的还是上一帧的坐标 */
    lv_obj_update_layout(obj);

    lv_area_t area;
    lv_obj_get_coords(obj, &area);

    /* 外扩 ext_draw_size：outline / shadow 画在 coords 之外，
     * 不包进窗口就会在面板上留下擦不掉的残影 */
    int32_t ext = lv_obj_calculate_ext_draw_size(obj, LV_PART_MAIN);
    if (ext > 0) lv_area_increase(&area, ext, ext);

    epd_region_flush_area(&area);
    lv_obj_invalidate(obj);
}

void epd_region_end(void)
{
    s_active = false;
    s_screen = NULL;
    s_focus  = NULL;

    epd_display_set_refresh_area(false, NULL);
    epd_display_set_flush_mode(EPD_FLUSH_MODE_PARTIAL_ALL);

    lv_obj_t *scr = lv_screen_active();
    if (scr) lv_obj_invalidate(scr);
}

bool epd_region_is_active(void)
{
    return s_active;
}
