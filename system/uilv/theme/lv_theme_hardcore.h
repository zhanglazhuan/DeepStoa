/**
 * @file lv_theme_hardcore.h
 *
 */

#ifndef LV_THEME_HARDCORE_H
#define LV_THEME_HARDCORE_H

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      INCLUDES
 *********************/
#include <lvgl.h>

/*********************
 *      DEFINES
 *********************/
#define LV_FONT_TINY   &lv_font_montserrat_18
#define LV_FONT_SMALL  &lv_font_montserrat_24
#define LV_FONT_NORMAL &lv_font_montserrat_32
#define LV_FONT_LARGE  &lv_font_montserrat_48   /* 计时器这类主角元素专用 */

#define EPOS_BOTTOM_BUTTON_HEIGHT 60

/**********************
 *      TYPEDEFS
 **********************/

/**********************
 * GLOBAL PROTOTYPES
 **********************/

/**
 * Initialize the eink theme
 * @param disp  pointer to display
 * @param dark_bg  true: dark background, false: light background (normal eink)
 * @param font  pointer to a font to use, or NULL to use LV_FONT_DEFAULT
 * @return a pointer to reference this theme later
 */
lv_theme_t * lv_theme_hardcore_init(lv_display_t * disp, bool dark_bg, const lv_font_t * font);

/**
 * Check if the theme is initialized
 * @return true if eink theme is initialized, false otherwise
 */
bool lv_theme_hardcore_is_inited(void);

/**
 * Get eink theme pointer
 * @return a pointer to the theme, or NULL if not initialized
 */
lv_theme_t * lv_theme_hardcore_get(void);

/**
 * Deinitialize the eink theme and free resources
 */
void lv_theme_hardcore_deinit(void);

/**********************
 *      MACROS
 **********************/

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /* LV_THEME_HARDCORE_H */
