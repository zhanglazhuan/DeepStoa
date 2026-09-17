#ifndef CHATBOT_VIEW_HISTORY_H
#define CHATBOT_VIEW_HISTORY_H

struct ChatbotApp;

typedef struct {
    lv_obj_t *list_container; // 列表容器
    lv_obj_t *delete_btn;     // 删除按钮
} ViewHisCtx;


void chatbot_view_history_init_registry(struct ChatbotApp* app);

#endif // CHATBOT_VIEW_HISTORY_H
