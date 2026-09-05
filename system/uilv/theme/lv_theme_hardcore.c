/**
 * @file lv_theme_hardcore.c
 *
 */

/*********************
 *      INCLUDES
 *********************/

#include "themes/lv_theme_private.h"
#include <lvgl.h>
#include "lv_theme_hardcore.h"

/*********************
 *      DEFINES
 *********************/
struct _my_theme_t;
typedef struct _my_theme_t my_theme_t;

/*
 * 原来用 LV_GLOBAL_DEFAULT()->theme_mono 是为了占用 LVGL 内部的全局槽位，
 * 搬到用户工程后直接用 static 指针代替，行为完全等价。
 */
static my_theme_t * theme_def = NULL;

#define RADIUS_DEFAULT 2

#define COLOR_FG      dark_bg ? lv_color_white() : lv_color_black()
#define COLOR_BG      dark_bg ? lv_color_black() : lv_color_white()

#define BORDER_W_NORMAL  1
#define BORDER_W_PR      3
#define BORDER_W_DIS     0
#define BORDER_W_FOCUS   1
#define BORDER_W_EDIT    2

#define PAD_DEF          4
#define PAD_SMALL        2
#define PAD_TINY         1

#define BORDER_WIDTH     1
#define OUTLINE_WIDTH    1

/**********************
 *      TYPEDEFS
 **********************/
typedef struct {
    lv_style_t scr;
    lv_style_t card;
    lv_style_t btn;
    lv_style_t cont;
    lv_style_t scrollbar;
    lv_style_t pr;
    lv_style_t inv;
    lv_style_t disabled;
    lv_style_t focus;
    lv_style_t edit;
    lv_style_t pad_gap;
    lv_style_t pad_zero;
    lv_style_t no_radius;
    lv_style_t radius_circle;
    lv_style_t large_border;
    lv_style_t large_line_space;
    lv_style_t underline;

#if LV_USE_TEXTAREA
    lv_style_t ta_cursor;
#endif

#if LV_USE_CHART
    lv_style_t chart_indic;
#endif

#if LV_USE_CALENDAR
    lv_style_t calendar_btnm_bg, calendar_btnm_day, calendar_header;
#endif

#if LV_USE_KEYBOARD
    lv_style_t keyboard_button_bg;
#endif

#if LV_USE_LIST
    lv_style_t list_bg, list_btn, list_item_grow;
#endif

} my_theme_styles_t;

struct _my_theme_t {
    lv_theme_t base;
    lv_color_t color_grey;
    my_theme_styles_t styles;
    bool inited;
};

/**********************
 *  STATIC PROTOTYPES
 **********************/
static void style_init_reset(lv_style_t * style);
static void theme_apply(lv_theme_t * th, lv_obj_t * obj);

/**********************
 *  STATIC VARIABLES
 **********************/

/**********************
 *      MACROS
 **********************/

/**********************
 *   STATIC FUNCTIONS
 **********************/

static void style_init(my_theme_t * theme, bool dark_bg, const lv_font_t * font)
{
    theme->color_grey  = lv_color_black();

    style_init_reset(&theme->styles.scrollbar);
    lv_style_set_bg_opa(&theme->styles.scrollbar, LV_OPA_COVER);
    lv_style_set_bg_color(&theme->styles.scrollbar, COLOR_FG);
    lv_style_set_width(&theme->styles.scrollbar,  PAD_DEF);

    style_init_reset(&theme->styles.scr);
    lv_style_set_bg_opa(&theme->styles.scr, LV_OPA_COVER);
    lv_style_set_bg_color(&theme->styles.scr, COLOR_BG);
    lv_style_set_text_color(&theme->styles.scr, COLOR_FG);
    lv_style_set_pad_row(&theme->styles.scr, PAD_DEF);
    lv_style_set_pad_column(&theme->styles.scr, PAD_DEF);
    lv_style_set_text_font(&theme->styles.scr, font);
    lv_style_set_pad_top(&theme->styles.scr, 40);
    lv_style_set_pad_bottom(&theme->styles.scr, 32);
    lv_style_set_pad_left(&theme->styles.scr, 16);
    lv_style_set_pad_right(&theme->styles.scr, 16);

    style_init_reset(&theme->styles.cont);
    lv_style_set_border_width(&theme->styles.cont, 0);
    lv_style_set_outline_width(&theme->styles.cont, 0);
    lv_style_set_pad_row(&theme->styles.cont, 0);
    lv_style_set_pad_column(&theme->styles.cont, 1);
    lv_style_set_pad_top(&theme->styles.cont, 0);
    lv_style_set_pad_bottom(&theme->styles.cont, 0);
    lv_style_set_bg_opa(&theme->styles.cont, LV_OPA_TRANSP);

    style_init_reset(&theme->styles.card);
    lv_style_set_bg_opa(&theme->styles.card, LV_OPA_COVER);
    lv_style_set_bg_color(&theme->styles.card, COLOR_BG);
    lv_style_set_border_color(&theme->styles.card, COLOR_FG);
    lv_style_set_radius(&theme->styles.card, 2);
    lv_style_set_border_width(&theme->styles.card, BORDER_W_NORMAL);
    lv_style_set_pad_all(&theme->styles.card, PAD_DEF);
    lv_style_set_pad_gap(&theme->styles.card, PAD_DEF);
    lv_style_set_text_color(&theme->styles.card, COLOR_FG);
    lv_style_set_line_width(&theme->styles.card, 2);
    lv_style_set_line_color(&theme->styles.card, COLOR_FG);
    lv_style_set_arc_width(&theme->styles.card, 2);
    lv_style_set_arc_color(&theme->styles.card, COLOR_FG);
    lv_style_set_outline_color(&theme->styles.card, COLOR_FG);
    lv_style_set_anim_duration(&theme->styles.card, 300);

    /* Dedicated Button style: transparent bg + 1px black border */
    style_init_reset(&theme->styles.btn);
    lv_style_set_bg_opa(&theme->styles.btn, LV_OPA_TRANSP);
    lv_style_set_border_width(&theme->styles.btn, BORDER_W_NORMAL);
    lv_style_set_border_color(&theme->styles.btn, COLOR_FG);
    lv_style_set_radius(&theme->styles.btn, 2);
    lv_style_set_text_color(&theme->styles.btn, COLOR_FG);

    style_init_reset(&theme->styles.pr);
    lv_style_set_border_width(&theme->styles.pr, BORDER_W_PR);

    /* "Inverse" for I1 e-ink: white bg + thick black border (not black fill) */
    /* "inv" = inverted：前景背景对调，用来让某个部件"被填充/被选中"的那一块
     * 在单色屏上真正凸显出来。
     *
     * 原来这里 bg_color 写的是 COLOR_BG、text_color 写的是 COLOR_FG ——
     * 也就是白底黑字，和普通配色一模一样，**根本没有反色**。后果是所有用它的
     * 地方都失去了状态指示：switch 打开和关闭长得一样（INDICATOR 是白的、
     * KNOB 也是白的，屏上只剩几条 1px 描边，看上去像"没有开关"）、checkbox
     * 勾没勾看不出、roller/dropdown 选中哪一行看不出、bar/slider 的填充量
     * 看不出、spinbox 光标看不见。
     *
     * arc_color / line_color 保持 COLOR_FG：lv_arc 的 INDICATOR 是用 arc_*
     * 画在部件背景上的，不走 bg_color；把它也翻成白色反而会让弧线消失。 */
    style_init_reset(&theme->styles.inv);
    lv_style_set_bg_opa(&theme->styles.inv, LV_OPA_COVER);
    lv_style_set_bg_color(&theme->styles.inv, COLOR_FG);        /* 黑底 */
    lv_style_set_border_width(&theme->styles.inv, BORDER_W_EDIT); /* 2px */
    lv_style_set_border_color(&theme->styles.inv, COLOR_FG);    /* 黑边（与黑底同色，等于无边） */
    lv_style_set_text_color(&theme->styles.inv, COLOR_BG);      /* 白字 */
    lv_style_set_line_color(&theme->styles.inv, COLOR_FG);
    lv_style_set_arc_color(&theme->styles.inv, COLOR_FG);
    lv_style_set_outline_color(&theme->styles.inv, COLOR_FG);

    style_init_reset(&theme->styles.disabled);
    lv_style_set_border_width(&theme->styles.disabled, BORDER_W_DIS);

    style_init_reset(&theme->styles.focus);
    lv_style_set_outline_width(&theme->styles.focus, 1);
    lv_style_set_outline_pad(&theme->styles.focus, BORDER_W_FOCUS);

    style_init_reset(&theme->styles.edit);
    lv_style_set_outline_width(&theme->styles.edit, BORDER_W_EDIT);

    style_init_reset(&theme->styles.large_border);
    lv_style_set_border_width(&theme->styles.large_border, BORDER_W_EDIT);

    style_init_reset(&theme->styles.pad_gap);
    lv_style_set_pad_gap(&theme->styles.pad_gap, PAD_DEF);

    style_init_reset(&theme->styles.pad_zero);
    lv_style_set_pad_all(&theme->styles.pad_zero, 0);
    lv_style_set_pad_gap(&theme->styles.pad_zero, 0);

    style_init_reset(&theme->styles.no_radius);
    lv_style_set_radius(&theme->styles.no_radius, 0);

    style_init_reset(&theme->styles.radius_circle);
    lv_style_set_radius(&theme->styles.radius_circle, LV_RADIUS_CIRCLE);

    style_init_reset(&theme->styles.large_line_space);
    lv_style_set_text_line_space(&theme->styles.large_line_space, 6);

    style_init_reset(&theme->styles.underline);
    lv_style_set_text_decor(&theme->styles.underline, LV_TEXT_DECOR_UNDERLINE);

#if LV_USE_TEXTAREA
    style_init_reset(&theme->styles.ta_cursor);
    lv_style_set_border_side(&theme->styles.ta_cursor, LV_BORDER_SIDE_LEFT);
    lv_style_set_border_color(&theme->styles.ta_cursor, COLOR_FG);
    lv_style_set_border_width(&theme->styles.ta_cursor, 2);
    lv_style_set_bg_opa(&theme->styles.ta_cursor, LV_OPA_TRANSP);
    lv_style_set_anim_duration(&theme->styles.ta_cursor, 0);
#endif

#if LV_USE_CHART
    style_init_reset(&theme->styles.chart_indic);
    lv_style_set_radius(&theme->styles.chart_indic, LV_RADIUS_CIRCLE);
    lv_style_set_size(&theme->styles.chart_indic, lv_display_dpx(theme->base.disp, 8), lv_display_dpx(theme->base.disp, 8));
    lv_style_set_bg_color(&theme->styles.chart_indic, COLOR_FG);
    lv_style_set_bg_opa(&theme->styles.chart_indic, LV_OPA_COVER);
#endif

#if LV_USE_CALENDAR
    style_init_reset(&theme->styles.calendar_btnm_bg);
    lv_style_set_pad_all(&theme->styles.calendar_btnm_bg, PAD_SMALL);
    lv_style_set_pad_gap(&theme->styles.calendar_btnm_bg, PAD_SMALL / 2);

    style_init_reset(&theme->styles.calendar_btnm_day);
    lv_style_set_border_width(&theme->styles.calendar_btnm_day, 1);
    lv_style_set_border_color(&theme->styles.calendar_btnm_day, theme->color_grey);
    lv_style_set_bg_color(&theme->styles.calendar_btnm_day, lv_color_white());
    lv_style_set_bg_opa(&theme->styles.calendar_btnm_day, LV_OPA_20);

    style_init_reset(&theme->styles.calendar_header);
    lv_style_set_pad_hor(&theme->styles.calendar_header, PAD_SMALL);
    lv_style_set_pad_top(&theme->styles.calendar_header, PAD_SMALL);
    lv_style_set_pad_bottom(&theme->styles.calendar_header, PAD_TINY);
    lv_style_set_pad_gap(&theme->styles.calendar_header, PAD_SMALL);
#endif

#if LV_USE_KEYBOARD
    style_init_reset(&theme->styles.keyboard_button_bg);
    lv_style_set_shadow_width(&theme->styles.keyboard_button_bg, 0);
    lv_style_set_radius(&theme->styles.keyboard_button_bg, RADIUS_DEFAULT);
#endif

#if LV_USE_LIST
    style_init_reset(&theme->styles.list_bg);
    lv_style_set_bg_opa(&theme->styles.list_bg, LV_OPA_TRANSP); // 透明背景
    lv_style_set_border_width(&theme->styles.list_bg, 0);       // 去除边框
    lv_style_set_radius(&theme->styles.list_bg, 0);             // 去除圆角
    lv_style_set_pad_all(&theme->styles.list_bg, 0);            // 外部 padding 为 0
    lv_style_set_pad_row(&theme->styles.list_bg, 20);           // item 间距 20
    lv_style_set_text_font(&theme->styles.list_bg, theme->base.font_normal); // 设置字体
    lv_style_set_text_align(&theme->styles.list_bg, LV_TEXT_ALIGN_CENTER);

    style_init_reset(&theme->styles.list_btn);
    lv_style_set_border_width(&theme->styles.list_btn, 0);
    lv_style_set_text_align(&theme->styles.list_btn, LV_TEXT_ALIGN_LEFT); 
    lv_style_set_bg_opa(&theme->styles.list_btn, LV_OPA_TRANSP); // 按钮背景设为透明
    lv_style_set_pad_all(&theme->styles.list_btn, PAD_SMALL);
    lv_style_set_pad_column(&theme->styles.list_btn, 8);         // 图标与文字的间距

    style_init_reset(&theme->styles.list_item_grow);
    lv_style_set_transform_width(&theme->styles.list_item_grow, PAD_DEF);
#endif

}

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

bool lv_theme_hardcore_is_inited(void)
{
    my_theme_t * theme = theme_def;
    if(theme == NULL) return false;
    return theme->inited;
}

void lv_theme_hardcore_deinit(void)
{
    my_theme_t * theme = theme_def;
    if(theme) {
        if(theme->inited) {
            lv_style_t * theme_styles = (lv_style_t *)(&(theme->styles));
            uint32_t i;
            for(i = 0; i < sizeof(my_theme_styles_t) / sizeof(lv_style_t); i++) {
                lv_style_reset(theme_styles + i);
            }
        }
        lv_free(theme_def);
        theme_def = NULL;
    }
}

lv_theme_t * lv_theme_hardcore_init(lv_display_t * disp, bool dark_bg, const lv_font_t * font)
{
    /*This trick is required only to avoid the garbage collection of
     *styles' data if LVGL is used in a binding (e.g. MicroPython)
     *In a general case styles could be in simple `static lv_style_t my_style...` variables*/
    if(!lv_theme_hardcore_is_inited()) {
        theme_def = lv_malloc_zeroed(sizeof(my_theme_t));
    }

    my_theme_t * theme = theme_def;

    theme->base.disp = disp;
    theme->base.font_small = LV_FONT_SMALL;
    theme->base.font_normal = LV_FONT_NORMAL;
    theme->base.font_large = LV_FONT_LARGE;
    theme->base.apply_cb = theme_apply;

    style_init(theme, dark_bg, font);

    if(disp == NULL || lv_display_get_theme(disp) == (lv_theme_t *) theme) lv_obj_report_style_change(NULL);

    theme->inited = true;

    return (lv_theme_t *)theme_def;
}

static void theme_apply(lv_theme_t * th, lv_obj_t * obj)
{
    LV_UNUSED(th);

    my_theme_t * theme = theme_def;
    lv_obj_t * parent = lv_obj_get_parent(obj);

    if(parent == NULL) {
        lv_obj_add_style(obj, &theme->styles.scr, 0);
        lv_obj_add_style(obj, &theme->styles.scrollbar, LV_PART_SCROLLBAR);
        return;
    }

    /* 精准类匹配：只拦截纯 lv_obj 容器，放过 Button/List 等子类 */
    if(lv_obj_get_class(obj) == &lv_obj_class) {
#if LV_USE_WIN
        /*Header*/
        if(lv_obj_check_type(parent, &lv_win_class) && lv_obj_get_child(parent, 0) == 0) {
            lv_obj_add_style(obj, &theme->styles.card, 0);
            lv_obj_add_style(obj, &theme->styles.no_radius, 0);
            return;
        }
        /*Content*/
        else if(lv_obj_check_type(parent, &lv_win_class) && lv_obj_get_child(parent, 1) == obj) {
            lv_obj_add_style(obj, &theme->styles.card, 0);
            lv_obj_add_style(obj, &theme->styles.no_radius, 0);
            lv_obj_add_style(obj, &theme->styles.scrollbar, LV_PART_SCROLLBAR);
            return;
        }
#endif
        lv_obj_add_style(obj, &theme->styles.card, 0);
        lv_obj_add_style(obj, &theme->styles.scrollbar, LV_PART_SCROLLBAR);
    }
#if LV_USE_BUTTON
    else if(lv_obj_check_type(obj, &lv_button_class)) {

#if LV_USE_LIST
        if(parent && lv_obj_check_type(parent, &lv_list_class)) {
            lv_obj_add_style(obj, &theme->styles.list_btn, 0);
            lv_obj_add_style(obj, &theme->styles.pr, LV_STATE_PRESSED);
            lv_obj_add_style(obj, &theme->styles.focus, LV_STATE_FOCUS_KEY);
            lv_obj_add_style(obj, &theme->styles.large_border, LV_STATE_EDITED);
            lv_obj_set_width(obj, LV_PCT(100));
            return;
        }
#endif

        /* Ordinary buttons: no theme styling — use ui_style_set_btn_*() instead */
    }
#endif

#if LV_USE_BUTTONMATRIX
    else if(lv_obj_check_type(obj, &lv_buttonmatrix_class)) {
#if LV_USE_MSGBOX
        if(lv_obj_check_type(parent, &lv_msgbox_class)) {
            lv_obj_add_style(obj, &theme->styles.pad_gap, 0);
            lv_obj_add_style(obj, &theme->styles.card, LV_PART_ITEMS);
            lv_obj_add_style(obj, &theme->styles.pr, LV_PART_ITEMS | LV_STATE_PRESSED);
            lv_obj_add_style(obj, &theme->styles.disabled, LV_PART_ITEMS | LV_STATE_DISABLED);
            lv_obj_add_style(obj, &theme->styles.underline, LV_PART_ITEMS | LV_STATE_FOCUS_KEY);
            lv_obj_add_style(obj, &theme->styles.large_border, LV_PART_ITEMS | LV_STATE_FOCUS_KEY);
            return;
        }
#endif
        lv_obj_add_style(obj, &theme->styles.card, 0);
        lv_obj_add_style(obj, &theme->styles.focus, LV_STATE_FOCUS_KEY);
        lv_obj_add_style(obj, &theme->styles.card, LV_PART_ITEMS);
        lv_obj_add_style(obj, &theme->styles.pr, LV_PART_ITEMS | LV_STATE_PRESSED);
        lv_obj_add_style(obj, &theme->styles.inv, LV_PART_ITEMS | LV_STATE_CHECKED);
        lv_obj_add_style(obj, &theme->styles.disabled, LV_PART_ITEMS | LV_STATE_DISABLED);
        lv_obj_add_style(obj, &theme->styles.underline, LV_PART_ITEMS | LV_STATE_FOCUS_KEY);
        lv_obj_add_style(obj, &theme->styles.large_border, LV_PART_ITEMS | LV_STATE_FOCUS_KEY);
    }
#endif

#if LV_USE_BAR
    else if(lv_obj_check_type(obj, &lv_bar_class)) {
        lv_obj_add_style(obj, &theme->styles.card, 0);
        lv_obj_add_style(obj, &theme->styles.pad_zero, 0);
        lv_obj_add_style(obj, &theme->styles.inv, LV_PART_INDICATOR);
        lv_obj_add_style(obj, &theme->styles.focus, LV_STATE_FOCUS_KEY);
    }
#endif

#if LV_USE_SLIDER
    else if(lv_obj_check_type(obj, &lv_slider_class)) {
        lv_obj_add_style(obj, &theme->styles.card, 0);
        lv_obj_add_style(obj, &theme->styles.pad_zero, 0);
        lv_obj_add_style(obj, &theme->styles.inv, LV_PART_INDICATOR);
        lv_obj_add_style(obj, &theme->styles.card, LV_PART_KNOB);
        lv_obj_add_style(obj, &theme->styles.radius_circle, LV_PART_KNOB);
        lv_obj_add_style(obj, &theme->styles.focus, LV_STATE_FOCUS_KEY);
        lv_obj_add_style(obj, &theme->styles.edit, LV_STATE_EDITED);
    }
#endif

#if LV_USE_TABLE
    else if(lv_obj_check_type(obj, &lv_table_class)) {
        lv_obj_add_style(obj, &theme->styles.scrollbar, LV_PART_SCROLLBAR);
        lv_obj_add_style(obj, &theme->styles.card, LV_PART_ITEMS);
        lv_obj_add_style(obj, &theme->styles.no_radius, LV_PART_ITEMS);
        lv_obj_add_style(obj, &theme->styles.pr, LV_PART_ITEMS | LV_STATE_PRESSED);
        lv_obj_add_style(obj, &theme->styles.focus, LV_STATE_FOCUS_KEY);
        lv_obj_add_style(obj, &theme->styles.inv, LV_PART_ITEMS | LV_STATE_FOCUS_KEY);
        lv_obj_add_style(obj, &theme->styles.edit, LV_STATE_EDITED);
    }
#endif

#if LV_USE_CHECKBOX
    else if(lv_obj_check_type(obj, &lv_checkbox_class)) {
        lv_obj_add_style(obj, &theme->styles.pad_gap, LV_PART_MAIN);
        lv_obj_add_style(obj, &theme->styles.card, LV_PART_INDICATOR);
        lv_obj_add_style(obj, &theme->styles.disabled, LV_PART_INDICATOR | LV_STATE_DISABLED);
        lv_obj_add_style(obj, &theme->styles.inv, LV_PART_INDICATOR | LV_STATE_CHECKED);
        lv_obj_add_style(obj, &theme->styles.pr, LV_PART_INDICATOR | LV_STATE_PRESSED);
        lv_obj_add_style(obj, &theme->styles.focus, LV_STATE_FOCUS_KEY);
        lv_obj_add_style(obj, &theme->styles.edit, LV_STATE_EDITED);
    }
#endif

#if LV_USE_SWITCH
    else if(lv_obj_check_type(obj, &lv_switch_class)) {
        lv_obj_add_style(obj, &theme->styles.card, 0);
        lv_obj_add_style(obj, &theme->styles.radius_circle, 0);
        lv_obj_add_style(obj, &theme->styles.pad_zero, 0);
        lv_obj_add_style(obj, &theme->styles.inv, LV_PART_INDICATOR);
        lv_obj_add_style(obj, &theme->styles.radius_circle, LV_PART_INDICATOR);
        lv_obj_add_style(obj, &theme->styles.card, LV_PART_KNOB);
        lv_obj_add_style(obj, &theme->styles.radius_circle, LV_PART_KNOB);
        lv_obj_add_style(obj, &theme->styles.pad_zero, LV_PART_KNOB);
        lv_obj_add_style(obj, &theme->styles.focus, LV_STATE_FOCUS_KEY);
        lv_obj_add_style(obj, &theme->styles.edit, LV_STATE_EDITED);
    }
#endif

#if LV_USE_CHART
    else if(lv_obj_check_type(obj, &lv_chart_class)) {
        lv_obj_add_style(obj, &theme->styles.card, 0);
        lv_obj_add_style(obj, &theme->styles.scrollbar, LV_PART_SCROLLBAR);
        lv_obj_add_style(obj, &theme->styles.chart_indic, LV_PART_INDICATOR);
        lv_obj_add_style(obj, &theme->styles.card, LV_PART_ITEMS);
        lv_obj_add_style(obj, &theme->styles.card, LV_PART_CURSOR);
        lv_obj_add_style(obj, &theme->styles.focus, LV_STATE_FOCUS_KEY);
    }
#endif

#if LV_USE_ROLLER
    else if(lv_obj_check_type(obj, &lv_roller_class)) {
        lv_obj_add_style(obj, &theme->styles.card, 0);
        lv_obj_add_style(obj, &theme->styles.large_line_space, 0);
        lv_obj_add_style(obj, &theme->styles.inv, LV_PART_SELECTED);
        lv_obj_add_style(obj, &theme->styles.focus, LV_STATE_FOCUS_KEY);
        lv_obj_add_style(obj, &theme->styles.edit, LV_STATE_EDITED);
    }
#endif

#if LV_USE_DROPDOWN
    else if(lv_obj_check_type(obj, &lv_dropdown_class)) {
        lv_obj_add_style(obj, &theme->styles.card, 0);
        lv_obj_add_style(obj, &theme->styles.pr, LV_STATE_PRESSED);
        lv_obj_add_style(obj, &theme->styles.focus, LV_STATE_FOCUS_KEY);
        lv_obj_add_style(obj, &theme->styles.edit, LV_STATE_EDITED);
    }
    else if(lv_obj_check_type(obj, &lv_dropdownlist_class)) {
        lv_obj_add_style(obj, &theme->styles.card, 0);
        lv_obj_add_style(obj, &theme->styles.large_line_space, 0);
        lv_obj_add_style(obj, &theme->styles.scrollbar, LV_PART_SCROLLBAR);
        lv_obj_add_style(obj, &theme->styles.inv, LV_PART_SELECTED | LV_STATE_CHECKED);
        lv_obj_add_style(obj, &theme->styles.pr, LV_PART_SELECTED | LV_STATE_PRESSED);
        lv_obj_add_style(obj, &theme->styles.focus, LV_STATE_FOCUS_KEY);
        lv_obj_add_style(obj, &theme->styles.edit, LV_STATE_EDITED);
    }
#endif

#if LV_USE_ARC
    else if(lv_obj_check_type(obj, &lv_arc_class)) {
        lv_obj_add_style(obj, &theme->styles.card, 0);
        lv_obj_add_style(obj, &theme->styles.inv, LV_PART_INDICATOR);
        lv_obj_add_style(obj, &theme->styles.pad_zero, LV_PART_INDICATOR);
        lv_obj_add_style(obj, &theme->styles.card, LV_PART_KNOB);
        lv_obj_add_style(obj, &theme->styles.radius_circle, LV_PART_KNOB);
        lv_obj_add_style(obj, &theme->styles.focus, LV_STATE_FOCUS_KEY);
        lv_obj_add_style(obj, &theme->styles.edit, LV_STATE_EDITED);
    }
#endif

#if LV_USE_TEXTAREA
    else if(lv_obj_check_type(obj, &lv_textarea_class)) {
        lv_obj_add_style(obj, &theme->styles.card, 0);
        lv_obj_add_style(obj, &theme->styles.scrollbar, LV_PART_SCROLLBAR);
        lv_obj_add_style(obj, &theme->styles.ta_cursor, LV_PART_CURSOR | LV_STATE_FOCUSED);
        lv_obj_add_style(obj, &theme->styles.focus, LV_STATE_FOCUSED);
        lv_obj_add_style(obj, &theme->styles.edit, LV_STATE_EDITED);
    }
#endif

#if LV_USE_CALENDAR
    else if(lv_obj_check_type(obj, &lv_calendar_class)) {
        lv_obj_add_style(obj, &theme->styles.card, 0);
        lv_obj_add_style(obj, &theme->styles.no_radius, 0);
        lv_obj_add_style(obj, &theme->styles.pr, LV_PART_ITEMS | LV_STATE_PRESSED);
        lv_obj_add_style(obj, &theme->styles.disabled, LV_PART_ITEMS | LV_STATE_DISABLED);
        lv_obj_add_style(obj, &theme->styles.focus, LV_STATE_FOCUS_KEY);
        lv_obj_add_style(obj, &theme->styles.edit, LV_STATE_EDITED);
        lv_obj_add_style(obj, &theme->styles.large_border, LV_PART_ITEMS | LV_STATE_FOCUS_KEY);
    }
#endif

#if LV_USE_KEYBOARD
    else if(lv_obj_check_type(obj, &lv_keyboard_class)) {
        lv_obj_add_style(obj, &theme->styles.card, 0);
        lv_obj_add_style(obj, &theme->styles.card, LV_PART_ITEMS);
        lv_obj_add_style(obj, &theme->styles.pr, LV_PART_ITEMS | LV_STATE_PRESSED);
        lv_obj_add_style(obj, &theme->styles.inv, LV_PART_ITEMS | LV_STATE_CHECKED);
        lv_obj_add_style(obj, &theme->styles.focus, LV_STATE_FOCUS_KEY);
        lv_obj_add_style(obj, &theme->styles.edit, LV_STATE_EDITED);
        lv_obj_add_style(obj, &theme->styles.large_border, LV_PART_ITEMS | LV_STATE_EDITED);
    }
#endif
#if LV_USE_LIST
    else if(lv_obj_check_type(obj, &lv_list_class)) {
        lv_obj_add_style(obj, &theme->styles.list_bg, 0);
        lv_obj_set_width(obj, LV_PCT(98));
        lv_obj_set_flex_grow(obj, 1);
        lv_obj_set_flex_align(obj, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_scroll_dir(obj, LV_DIR_VER);
        lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
        return;
    }
    else if(lv_obj_check_type(obj, &lv_list_text_class)) {
        /* placeholder — list text needs no extra style */
    }
    /* 注意：lv_list_button_class 的样式已在 LV_USE_BUTTON 分支中通过父对象拦截处理，
     * 此处不再重复，否则会因为 LVGL v9 的多态匹配顺序而永远不被执行。 */
#endif
#if LV_USE_MSGBOX
    else if(lv_obj_check_type(obj, &lv_msgbox_class)) {
        lv_obj_add_style(obj, &theme->styles.card, 0);
        return;
    }
#endif
#if LV_USE_SPINBOX
    else if(lv_obj_check_type(obj, &lv_spinbox_class)) {
        lv_obj_add_style(obj, &theme->styles.card, 0);
        lv_obj_add_style(obj, &theme->styles.inv, LV_PART_CURSOR);
        lv_obj_add_style(obj, &theme->styles.focus, LV_STATE_FOCUS_KEY);
        lv_obj_add_style(obj, &theme->styles.edit, LV_STATE_EDITED);
    }
#endif
#if LV_USE_TILEVIEW
    else if(lv_obj_check_type(obj, &lv_tileview_class)) {
        lv_obj_add_style(obj, &theme->styles.scr, 0);
        lv_obj_add_style(obj, &theme->styles.scrollbar, LV_PART_SCROLLBAR);
    }
    else if(lv_obj_check_type(obj, &lv_tileview_tile_class)) {
        lv_obj_add_style(obj, &theme->styles.scrollbar, LV_PART_SCROLLBAR);
    }
#endif

#if LV_USE_LED
    else if(lv_obj_check_type(obj, &lv_led_class)) {
        lv_obj_add_style(obj, &theme->styles.card, 0);
    }
#endif

#if LV_USE_IMAGE
    else if(lv_obj_check_type(obj, &lv_image_class)) {
        // 判断如果这个 Image 是放在 List 按钮里的（官方 API 把 Symbol 当作 Image 处理）
        if(parent && lv_obj_check_type(parent, &lv_list_button_class)) {
            // 强制给所有的图标分配绝对相等的 30px 宽度！
            lv_obj_set_width(obj, 30);
            // 让不同宽窄的符号在 30px 的盒子里居中
            lv_image_set_inner_align(obj, LV_IMAGE_ALIGN_CENTER);
        }
    }
#endif
}

lv_theme_t * lv_theme_hardcore_get(void)
{
    if(!lv_theme_hardcore_is_inited()) {
        return NULL;
    }
    return (lv_theme_t *)theme_def;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

static void style_init_reset(lv_style_t * style)
{
    if(lv_theme_hardcore_is_inited()) {
        lv_style_reset(style);
    }
    else {
        lv_style_init(style);
    }
}

