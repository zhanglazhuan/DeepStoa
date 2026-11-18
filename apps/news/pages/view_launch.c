#include <lvgl.h>
#include "../app.h"
#include "../view.h"
#include "view_launch.h"

/* 声明导出的图片数据结构 */
extern const lv_image_dsc_t app_news_cover;

static lv_obj_t * build_launch_page(void *arg)
{
    NewsApp *app = (NewsApp *)arg;

    /* 创建基础页面容器 */
    lv_obj_t *page = lv_obj_create(NULL);
    lv_obj_set_size(page, LV_PCT(100), LV_PCT(100));

    /* 针对墨水屏，清除滚动条并设置纯白背景 */
    lv_obj_remove_style_all(page);
    lv_obj_set_style_bg_color(page, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(page, LV_OPA_COVER, 0);

    /* 创建图片对象并居中 */
    lv_obj_t *img = lv_image_create(page);
    lv_image_set_src(img, &app_news_cover);
    lv_obj_align(img, LV_ALIGN_CENTER, 0, 0);

    return page;
}

void news_view_launch_init_registry(NewsApp *app)
{
    PAGE_REGISTE(app, PAGE_NEWS_LAUNCH, build_launch_page);
}
