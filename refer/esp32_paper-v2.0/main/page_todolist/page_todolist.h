#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void page_todolist_init(void);
void page_todolist_show(void);

// MCP helpers
bool todolist_add_item_from_mcp(const char* content,
                               int type,
                               int remind_enabled,
                               int remind_month,
                               int remind_day,
                               int remind_hour,
                               int remind_minute);
void todolist_open_from_mcp(void);

#ifdef __cplusplus
}
#endif
