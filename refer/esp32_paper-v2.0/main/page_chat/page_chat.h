#ifndef PAGE_CHAT_H
#define PAGE_CHAT_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void page_chat_show(void);
bool page_chat_is_active(void);
void page_chat_request_redraw(void);

#ifdef __cplusplus
}
#endif

#endif
