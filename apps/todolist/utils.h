
#ifndef TODOLIST_UTILS_H
#define TODOLIST_UTILS_H

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Map LVGL event code to a readable string */
const char* lv_event_code_str(lv_event_code_t code);
void ta_debug_event_cb(lv_event_t * e);
void setup_custom_keyboard(lv_obj_t * kb);

/**
 * @brief 破坏性操作的二次确认弹窗（全 App 统一一份）
 *
 * 只有 Cancel / 确认两个出口 —— 原来同时给关闭 ×、Cancel、Delete 三个出口，
 * 用户要在三个东西里挑一个「取消」。
 *
 * @param title      标题
 * @param message    正文
 * @param confirm_txt 确认按钮文案（如 "Delete"）
 * @param on_confirm 点确认时的回调，user_data 原样透传
 * @param user_data  透传给回调（用 id 而不是指针，见 model 的数组压缩语义）
 */
typedef void (*todolist_confirm_cb_t)(void *user_data);

void todolist_confirm_dialog(const char *title, const char *message,
                             const char *confirm_txt,
                             todolist_confirm_cb_t on_confirm, void *user_data);

#ifdef __cplusplus
}
#endif

#endif /* TODOLIST_UTILS_H */