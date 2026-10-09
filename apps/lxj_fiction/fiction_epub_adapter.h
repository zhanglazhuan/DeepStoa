#ifndef LXJ_FICTION_EPUB_ADAPTER_H
#define LXJ_FICTION_EPUB_ADAPTER_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Build or reuse the text cache for an EPUB.  The parser owns the ZIP/XML
 * details; the C wrapper keeps the LVGL application independent of C++ ABI. */
bool fiction_epub_prepare_cache(const char *epub_path,
                                char *out_cache_path,
                                size_t out_cache_path_len,
                                char *error_message,
                                size_t error_message_len);

#ifdef __cplusplus
}
#endif

#endif /* LXJ_FICTION_EPUB_ADAPTER_H */
