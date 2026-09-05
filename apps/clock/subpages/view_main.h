#ifndef CLOCK_VIEW_MAIN_H
#define CLOCK_VIEW_MAIN_H

#include <stddef.h>
#include <stdint.h>

#include "../clock_app.h"

#ifdef __cplusplus
extern "C" {
#endif

void clock_view_main_init_registry(struct ClockApp *app);
void clock_view_main_rebuild_alarm_list(struct ClockApp *app);

/** 重画时长 chip：文案 + 选中态 + 计时中禁用。 */
void clock_view_main_refresh_timer_chips(struct ClockApp *app);

/** 时长文案（"25m" / "2h"）。建立和刷新共用，避免两处格式不一致。 */
void clock_view_main_duration_text(uint16_t minutes, char *buf, size_t len);

void clock_view_main_show_max_alarm_warning(void);

/**
 * @brief 就地更新某一行的"停用"删除线（不重建列表）。
 * @param sw       该行右侧的开关对象；函数据此反查同行的两个文字标签
 * @param enabled  true = 去掉删除线，false = 加上
 */
void clock_view_main_set_row_enabled(lv_obj_t *sw, bool enabled);

#ifdef __cplusplus
}
#endif

#endif /* CLOCK_VIEW_MAIN_H */
