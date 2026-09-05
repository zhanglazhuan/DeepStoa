/*******************************************************************************
 * Size: 32 px
 * Bpp: 1
 * Opts: --no-compress --bpp 1 --size 32 --font D:/Codes/epos/tools/temp/custom_icons.ttf -r 0xe858 --format lvgl -o D:\Codes\epos\tools\icom_alarm.c
 ******************************************************************************/

#ifdef LV_LVGL_H_INCLUDE_SIMPLE
#include "lvgl.h"
#else
#include <lvgl.h>
#endif

#ifndef ICOM_ALARM
#define ICOM_ALARM 1
#endif

#if ICOM_ALARM

/*-----------------
 *    BITMAPS
 *----------------*/

/*Store the image of the glyphs*/
static LV_ATTRIBUTE_LARGE_CONST const uint8_t glyph_bitmap[] = {
    /* U+E858 "" */
    0xc, 0x0, 0x3, 0x1, 0xc0, 0x0, 0x38, 0x38,
    0x1f, 0x81, 0xc7, 0xf, 0xff, 0xe, 0xe1, 0xe0,
    0x78, 0x7c, 0x78, 0x1, 0xc3, 0x6, 0x0, 0x6,
    0x0, 0xc0, 0x0, 0x30, 0x1c, 0x0, 0x3, 0x81,
    0x80, 0x2, 0x18, 0x38, 0x0, 0x71, 0x83, 0x0,
    0xf, 0xc, 0x30, 0x40, 0xe0, 0xc3, 0xe, 0x1c,
    0xc, 0x30, 0xf3, 0x80, 0xc3, 0x7, 0xf0, 0xc,
    0x30, 0x3e, 0x0, 0xc3, 0x81, 0xc0, 0x18, 0x18,
    0x8, 0x1, 0x81, 0xc0, 0x0, 0x38, 0xc, 0x0,
    0x3, 0x0, 0x60, 0x0, 0x60, 0x7, 0x80, 0x1c,
    0x0, 0x1e, 0x7, 0x80, 0x0, 0xff, 0xf0, 0x0,
    0x3, 0xf8, 0x0
};


/*---------------------
 *  GLYPH DESCRIPTION
 *--------------------*/

static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {
    {.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0} /* id = 0 reserved */,
    {.bitmap_index = 0, .adv_w = 512, .box_w = 28, .box_h = 26, .ofs_x = 2, .ofs_y = -5}
};

/*---------------------
 *  CHARACTER MAPPING
 *--------------------*/



/*Collect the unicode lists and glyph_id offsets*/
static const lv_font_fmt_txt_cmap_t cmaps[] =
{
    {
        .range_start = 59480, .range_length = 1, .glyph_id_start = 1,
        .unicode_list = NULL, .glyph_id_ofs_list = NULL, .list_length = 0, .type = LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY
    }
};



/*--------------------
 *  ALL CUSTOM DATA
 *--------------------*/

#if LVGL_VERSION_MAJOR == 8
/*Store all the custom data of the font*/
static  lv_font_fmt_txt_glyph_cache_t cache;
#endif

#if LVGL_VERSION_MAJOR >= 8
static const lv_font_fmt_txt_dsc_t font_dsc = {
#else
static lv_font_fmt_txt_dsc_t font_dsc = {
#endif
    .glyph_bitmap = glyph_bitmap,
    .glyph_dsc = glyph_dsc,
    .cmaps = cmaps,
    .kern_dsc = NULL,
    .kern_scale = 0,
    .cmap_num = 1,
    .bpp = 1,
    .kern_classes = 0,
    .bitmap_format = 0,
#if LVGL_VERSION_MAJOR == 8
    .cache = &cache
#endif
};



/*-----------------
 *  PUBLIC FONT
 *----------------*/

/*Initialize a public general font descriptor*/
#if LVGL_VERSION_MAJOR >= 8
const lv_font_t icom_alarm = {
#else
lv_font_t icom_alarm = {
#endif
    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,    /*Function pointer to get glyph's data*/
    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,    /*Function pointer to get glyph's bitmap*/
    .line_height = 26,          /*The maximum line height required by the font*/
    .base_line = 5,             /*Baseline measured from the bottom of the line*/
#if !(LVGL_VERSION_MAJOR == 6 && LVGL_VERSION_MINOR == 0)
    .subpx = LV_FONT_SUBPX_NONE,
#endif
#if LV_VERSION_CHECK(7, 4, 0) || LVGL_VERSION_MAJOR >= 8
    .underline_position = 0,
    .underline_thickness = 0,
#endif
    .dsc = &font_dsc,          /*The custom font data. Will be accessed by `get_glyph_bitmap/dsc` */
#if LV_VERSION_CHECK(8, 2, 0) || LVGL_VERSION_MAJOR >= 9
    .fallback = NULL,
#endif
    .user_data = NULL,
};



#endif /*#if ICOM_ALARM*/

