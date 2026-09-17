// view_main.h
#ifndef CHATBOT_VIEW_MAIN_H
#define CHATBOT_VIEW_MAIN_H

void chatbot_view_main_init_registry(struct ChatbotApp* app);
void chatbot_view_add_chat_bubble(lv_obj_t * parent, const char * text, MsgRole role);

#endif // CHATBOT_VIEW_MAIN_H
