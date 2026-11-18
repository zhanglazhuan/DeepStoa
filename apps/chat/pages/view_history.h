#ifndef CHAT_VIEW_HISTORY_H
#define CHAT_VIEW_HISTORY_H

struct ChatApp;

typedef struct {
    lv_obj_t *list_container; // 列表容器
    lv_obj_t *delete_btn;     // 删除按钮
} ViewHisCtx;


void chat_view_history_init_registry(struct ChatApp* app);

#endif // CHAT_VIEW_HISTORY_H
