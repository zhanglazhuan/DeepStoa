// view_main.h
#ifndef CHAT_VIEW_MAIN_H
#define CHAT_VIEW_MAIN_H

void chat_view_main_init_registry(struct ChatApp* app);
void chat_view_add_chat_bubble(lv_obj_t * parent, const char * text, MsgRole role);

#endif // CHAT_VIEW_MAIN_H
