/**
 * @file lv_epd_region.h
 * @brief 墨水屏「局部刷新区」helper
 *
 * 墨水屏整屏刷新会明显闪一下。对于播放进度条、播放/暂停图标这类高频小范围
 * 变化，必须把面板更新限制在一个小窗口内。底层能力由 display_control 提供，
 * 但用起来有固定的三步顺序，很容易写错，这里封装成三个函数。
 *
 * 用法：
 *
 *   页面构建完成后           epd_region_begin(page.screen);
 *   进度条变了               epd_region_flush_obj(progress_zone);
 *   播放图标变了             epd_region_flush_obj(btn_play);
 *   页面销毁 / 返回          epd_region_end();
 *
 * 两个互不相邻的区域交替调用 flush_obj 即可，不需要取并集 —— 每次调用会把
 * 面板窗口切到该对象的矩形。同一帧内只应该 flush 一个区域。
 *
 * 注意：
 *   - begin() 会先用一整帧把静态内容打到面板并同步差分基准，这一帧是整屏的
 *     （不可避免，也只有这一次）。之后才进入窗口模式。
 *   - begin() 内部通过 lv_async_call 延后执行，因此可以安全地在页面 builder
 *     或事件回调里调用，不会重入 LVGL 的刷新流程。
 *   - 在 begin() 生效之前调用 flush_obj()，会退化成一次普通整屏刷新，不会崩。
 */

#ifndef LV_EPD_REGION_H
#define LV_EPD_REGION_H

#include <stdbool.h>
#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 进入局部刷新模式（先打一整帧基准，之后只推窗口内的像素） */
void epd_region_begin(lv_obj_t *screen);

/**
 * 同上，但基准帧打完后立刻把窗口固定到 focus_obj。
 * 适合"整页只有一块会反复变"的场景 —— 比如键盘弹出后只有输入框在变。
 * 之后不需要再调 flush_obj()，窗口会一直保持。
 */
void epd_region_begin_focus(lv_obj_t *screen, lv_obj_t *focus_obj);

/** 把面板刷新窗口切到 obj 的矩形，并让它重绘 */
void epd_region_flush_obj(lv_obj_t *obj);

/** 同上，但直接给屏幕坐标矩形 */
void epd_region_flush_area(const lv_area_t *area);

/** 退出局部刷新模式，恢复整屏 PARTIAL_ALL 并刷新一次 */
void epd_region_end(void);

/** 当前是否已经处于局部刷新模式 */
bool epd_region_is_active(void);

#ifdef __cplusplus
}
#endif

#endif /* LV_EPD_REGION_H */
