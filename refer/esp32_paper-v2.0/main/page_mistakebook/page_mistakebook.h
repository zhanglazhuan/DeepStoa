#pragma once

#include <stdbool.h>

#ifdef __cplusplus
#include <string>
#endif

#ifdef __cplusplus
extern "C" {
#endif

void page_mistakebook_show(void);
bool mistakebook_add_item_from_mcp(const char* subject,
                                   const char* title,
                                   const char* question_text,
                                   const char* student_answer,
                                   const char* correct_answer,
                                   const char* knowledge_point,
                                   const char* error_type,
                                   const char* explanation,
                                   int review_pending);
void mistakebook_open_from_mcp(void);

#ifdef __cplusplus
}

std::string mistakebook_get_items_json(bool review_pending_only);
#endif
