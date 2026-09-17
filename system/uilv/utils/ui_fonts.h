#ifndef UI_FONTS_H
#define UI_FONTS_H

#include <lvgl.h>

// --- 声明外部只读图标字体 (来自 Flash) ---
extern const lv_font_t icon_grid;
extern const lv_font_t icon_clock;
extern const lv_font_t icon_landscape;
extern const lv_font_t icon_info;
extern const lv_font_t icon_doing;
extern const lv_font_t icon_edit;
extern const lv_font_t icon_draggable;
extern const lv_font_t icon_wifi;
extern const lv_font_t icon_battery;
extern const lv_font_t icom_alarm;

// 👇 【新增】：声明一个全局可用的、挂载了所有图标的 RAM 字体
extern lv_font_t custom_font_normal; 

// --- 定义所有自定义 Symbol 宏 ---
#define MY_SYMBOL_GRID    "\xEE\x98\xBE"
#define MY_SYMBOL_CLOCK   "\xEE\xA5\x87"
#define MY_SYMBOL_LANDSCAPE "\xEE\x99\xB1"
#define MY_SYMBOL_INFO    "\xEE\xA0\x9E"
#define MY_SYMBOL_DOING   "\xEE\x98\x96"
#define MY_SYMBOL_EDIT    "\xEE\x98\x9D"
#define MY_SYMBOL_DRAGGABLE "\xEE\x98\x83"
#define MY_SYMBOL_ALARM "\xEE\xA1\x98" /* U+E858 */

#define MY_SYMBOL_WIFI_1_BAR  "\xEE\x93\x8A" /* U+E4CA */
#define MY_SYMBOL_WIFI_2_BAR  "\xEE\x93\x99" /* U+E4D9 */
#define MY_SYMBOL_WIFI_3_BAR  "\xEE\xBC\x96" /* U+EF16 */
#define MY_SYMBOL_WIFI_3_BAR_ALERT "\xEE\xBC\x9B" /* U+EF1B */
#define MY_SYMBOL_WIFI_OFF    "\xEE\x99\x88" /* U+E648 */

#define MY_SYMBOL_BATTERY_0 "\xEF\x8C\x8D" /* U+F30D */
#define MY_SYMBOL_BATTERY_1 "\xEF\x89\x97" /* U+F257 */
#define MY_SYMBOL_BATTERY_2 "\xEF\x89\x96" /* U+F256 */
#define MY_SYMBOL_BATTERY_3 "\xEF\x89\x95" /* U+F255 */
#define MY_SYMBOL_BATTERY_4 "\xEF\x89\x94" /* U+F254 */
#define MY_SYMBOL_BATTERY_5 "\xEF\x89\x93" /* U+F253 */
#define MY_SYMBOL_BATTERY_6 "\xEF\x89\x92" /* U+F252 */
#define MY_SYMBOL_BATTERY_FULL "\xEF\x89\x8F" /* U+F24F */
#define MY_SYMBOL_BATTERY_CHARGING "\xEF\x8C\x85" /* U+F305 */
#define MY_SYMBOL_BATTERY_FULL_CHARGING "\xEF\x89\x90" /* U+F250 */

void ui_fonts_init(void);

#endif // UI_FONTS_H