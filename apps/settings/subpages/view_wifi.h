#ifndef SETTINGS_VIEW_WIFI_H
#define SETTINGS_VIEW_WIFI_H

#include <lvgl.h>

struct SettingsApp;

typedef struct {
    lv_obj_t *sw;              /* Wi-Fi 开关 */
    lv_obj_t *status_lbl;      /* 忙碌 / 错误提示行 */
    lv_obj_t *connected_row;   /* 当前连接 */
    lv_obj_t *connected_lbl;
    lv_obj_t *connected_icon;
    lv_obj_t *scanned_cont;    /* 扫描结果列表 */
    lv_obj_t *scan_btn;
} ViewWifiCtx;

void settings_view_wifi_init_registry(struct SettingsApp *app);

/* controller 拿到异步结果后回调这三个来刷新界面。
 * 不在 Wi-Fi 页时全是安全的空操作。 */
void settings_view_wifi_update_scanned_list(struct SettingsApp *app);
void settings_view_wifi_update_connected(struct SettingsApp *app);
void settings_view_wifi_update_visibility(struct SettingsApp *app);

#endif /* SETTINGS_VIEW_WIFI_H */
