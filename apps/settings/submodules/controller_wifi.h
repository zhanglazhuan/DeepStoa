/**
 * @file controller_wifi.h
 * @brief Wi-Fi 交互控制器
 *
 * Ported from D:\Codes\EPOS\epos\apps\settings\submodules\controller_wifi.c
 *
 * 相对 EPOS 的关键改动：
 *   EPOS 用 ZBUS 监听 + k_work 把事件甩到工作队列。DeepStoa 这边没有 ZBUS，
 *   而且 wifi_manager 的 wifi_scan() / wifi_connect() 是**阻塞**的
 *   （分别最多 10s / 15s）。直接在 LVGL 线程里调，屏幕会僵住十几秒 ——
 *   墨水屏上更糟，因为连"转圈"都没有，看起来就是死机。
 *
 *   所以这里起一个专门的 FreeRTOS 任务干阻塞活，结果用 lv_async_call
 *   弹回 LVGL 线程再更新 model 和 UI。所有对外函数都是**立刻返回**的。
 *
 * 线程规则：
 *   - 本文件所有 public 函数只能在 LVGL 线程调用；
 *   - model 只在 LVGL 线程写；
 *   - worker 任务只碰自己的请求/结果缓冲，绝不碰 model 和 LVGL 对象。
 */

#ifndef SETTINGS_CONTROLLER_WIFI_H
#define SETTINGS_CONTROLLER_WIFI_H

#include <stdbool.h>
#include <lvgl.h>

struct SettingsApp;

void settings_controller_wifi_init(struct SettingsApp *app);
void settings_controller_wifi_deinit(void);

/** 开 / 关电台。异步，立刻返回。 */
void settings_controller_wifi_set_enabled(bool on);

/** LVGL 事件适配：绑在 Wi-Fi 开关上 */
void settings_controller_wifi_toggle(lv_event_t *e);

/** 异步扫描周边 AP。忙的时候会被忽略。 */
void settings_controller_wifi_scan(void);

/** 异步连接。password 传 NULL 或空串表示用已保存的凭据。 */
void settings_controller_wifi_connect(const char *ssid, const char *password);

void settings_controller_wifi_disconnect(void);

/** 忘记某个已保存的网络（从 wifi_store 里删掉这一条）。
 *  密码输错过就得靠它，否则会一直拿旧密码自动重连。 */
void settings_controller_wifi_forget(const char *ssid);

/** 扫描或连接进行中 —— UI 用它禁掉重复操作 */
bool settings_controller_wifi_busy(void);

/** 这个 SSID 是否就是 wifi_manager 里存着的那一组凭据 */
bool settings_controller_wifi_is_saved(const char *ssid);

#endif  /* SETTINGS_CONTROLLER_WIFI_H */
