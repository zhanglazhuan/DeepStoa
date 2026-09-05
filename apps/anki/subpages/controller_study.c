#include <time.h>
#include <lvgl.h>
#include "controller_study.h"
#include "controller_settings.h"
#include "../controller.h"
#include "../model.h"
#include "../view.h"
#include "../storage.h"
#include "view_study.h"

void anki_controller_on_show_answer(lv_event_t *e) {
    AnkiApp *app = lv_event_get_user_data(e);
    app->model->is_showing_answer = true;
    anki_view_study_card_flip(app);
}

void anki_controller_on_rate_card(lv_event_t *e) {
    AnkiApp *app = lv_event_get_user_data(e);
    int rating = (int)(intptr_t)lv_obj_get_user_data(lv_event_get_current_target(e));
    
    // 1. 获取复习设置（自动判断使用全局还是牌组专属），以及当前正在背的卡片
    AnkiSettings *settings = anki_controller_settings_get_config(app);
    CardIndex *card = &app->model->today_queue[app->model->active_card_idx];
    
    // 2. 根据 rating (0: Hard, 1: Good, 2: Easy) 计算下一次复习间隔
    int new_interval = card->interval;
    
    if (card->status == 0) {
        // 新卡片首次学习
        if (rating == 0) new_interval = settings->hard_interval;
        else if (rating == 1) new_interval = settings->good_interval;
        else if (rating == 2) new_interval = settings->easy_interval;
        
        if (rating > 0) {
            card->status = 1; // 评分非 Hard，转为学习中/已毕业状态
        }
    } else {
        // 已有复习记录的卡片 (简化版 SM-2 算法，基于旧间隔翻倍计算)
        if (rating == 0) new_interval = (card->interval * 12) / 10;      // Hard: 1.2倍
        else if (rating == 1) new_interval = (card->interval * 25) / 10; // Good: 2.5倍
        else if (rating == 2) new_interval = (card->interval * 30) / 10; // Easy: 3.0倍
    }
    
    if (new_interval <= 0) new_interval = 1;
    card->interval = new_interval;
    
    // 计算下次到期时间戳 (以秒为单位)
    time_t now = time(NULL);
    if (now > 0) {
        card->due_timestamp = now + (new_interval * 24 * 60 * 60);
    } else {
        // 如果系统 RTC 时间未就绪，退级处理：在原有时间上累加
        card->due_timestamp += (new_interval * 24 * 60 * 60); 
    }
    
    // 同步元数据游标状态
    anki_storage_sync_meta(app->model);
    
    // 持久化当前卡片到 FlashDB
    anki_model_update_card(app->model, card);
    anki_controller_sync(app); // 重新计算包含子牌组的剩余量，避免退回列表页进度没变
    
    // 3. 将游标推向下一张卡片
    app->model->active_card_idx++;

    if (!anki_model_load_next_card(app->model)) {
        anki_view_study_show_finished_msgbox(app);
    } else {
        app->model->is_showing_answer = false;
        anki_view_study_refresh_card(app);
    }
}
