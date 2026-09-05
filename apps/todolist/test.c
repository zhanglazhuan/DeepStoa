#include <stdlib.h>
#include <stdio.h>

#include "esp_log.h"

#include "lvgl.h"
#include "test.h"
#include "utils.h"

static const char __attribute__((unused)) *TAG = "todolist_test";

static void gesture_event_cb(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * obj = lv_event_get_target(e);
    
    // printf("事件触发: 代码=%s (%d)\n", lv_event_code_str(code), code);
    
    if(code == LV_EVENT_GESTURE) {
        lv_indev_t * indev = lv_indev_active();
        if(!indev) {
            printf("无法获取输入设备\n");
            return;
        }
        
        lv_dir_t dir = lv_indev_get_gesture_dir(indev);
        printf("手势方向: %d (左=1, 右=2, 上=4, 下=8)\n", dir);
        lv_indev_wait_release(lv_indev_active());
        
        // 根据手势方向改变对象的背景颜色
        if(dir == LV_DIR_LEFT) {
            printf("左滑手势被触发!\n");
            lv_obj_set_style_bg_color(obj, lv_color_hex(0xFF0000), 0); // 红色
        } 
        else if(dir == LV_DIR_RIGHT) {
            printf("右滑手势被触发!\n");
            lv_obj_set_style_bg_color(obj, lv_color_hex(0x00FF00), 0); // 绿色
        }
        else if(dir == LV_DIR_TOP) {
            printf("上滑手势被触发!\n");
            lv_obj_set_style_bg_color(obj, lv_color_hex(0x0000FF), 0); // 蓝色
        }
        else if(dir == LV_DIR_BOTTOM) {
            printf("下滑手势被触发!\n");
            lv_obj_set_style_bg_color(obj, lv_color_hex(0xFFFF00), 0); // 黄色
        }
    }
    else if(code == LV_EVENT_CLICKED) {
        printf("点击事件被触发!\n");
        lv_obj_set_style_bg_color(obj, lv_color_hex(0xFFFFFF), 0); // 白色
    }
}

int t_gesture(void)
{
    // 打印LVGL配置信息
    printf("LVGL手势配置:\n");
    printf("LV_USE_INDEV_GESTURE: 已启用\n");
    printf("LV_INDEV_DEF_GESTURE_LIMIT: %d\n", 20);
    printf("LV_INDEV_DEF_GESTURE_MIN_VELOCITY: %d\n", 3);
    
    // 创建一个简单的测试对象
    lv_obj_t * obj = lv_obj_create(lv_screen_active());
    lv_obj_set_size(obj, 200, 200);
    lv_obj_align(obj, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(obj, lv_color_hex(0xAAAAAA), 0); // 灰色背景
    
    // 添加文本标签
    lv_obj_t * label = lv_label_create(obj);
    lv_label_set_text(label, "在此区域上滑动测试手势\n(左/右/上/下)");
    lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);
    
    // 关键：添加CLICKABLE标志
    lv_obj_add_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    
    // 注册手势事件回调
    lv_obj_add_event_cb(obj, gesture_event_cb, LV_EVENT_ALL, NULL);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_GESTURE_BUBBLE);
    
    // 打印调试信息
    printf("手势测试程序已启动\n");
    printf("请在灰色方块上滑动鼠标来测试手势识别\n");
    printf("左滑: 红色, 右滑: 绿色, 上滑: 蓝色, 下滑: 黄色, 点击: 白色\n");
    printf("使用方法: 按住鼠标左键并拖动\n");
    return 0;
}

void todolist_model_mock_tasks(TodoListModel* model) {
    if(model->todo_task_count >= TODOLIST_MAX_TASKS) return;
    
    for(int32_t i = 0; i < 5; i++) {
        todolist_task_t *t = &(model->todo_tasks[model->todo_task_count]);
        t->id = model->_id_generator++;
        strncpy(t->title, "Mock Task balabala", TODOLIST_TITLE_MAX - 1);
        t->title[TODOLIST_TITLE_MAX - 1] = '\0';
        t->estimate_s = 1800; // 默认30分钟
        t->elapsed_s = 0;
        model->todo_task_count++;

        printf("Mocked Task %ld: ID=%lu, Title=%s, Estimate=%lumin\n",
               (long)i, (unsigned long)t->id, t->title, (unsigned long)(t->estimate_s / 60));
    }

    return;
}