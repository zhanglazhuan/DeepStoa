#ifndef LXJ_FICTION_EPUB_RICH_ADAPTER_H
#define LXJ_FICTION_EPUB_RICH_ADAPTER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

bool fiction_epub_rich_open(const char *epub_path);
void fiction_epub_rich_close(void);
bool fiction_epub_rich_read_page(char *out_text, size_t out_text_len);
bool fiction_epub_rich_next(void);
bool fiction_epub_rich_prev(void);
int fiction_epub_rich_toc_count(void);
bool fiction_epub_rich_toc_title(int index, char *out_title, size_t out_title_len);
bool fiction_epub_rich_jump_to_toc(int index);
bool fiction_epub_rich_read_page_image(uint8_t **out_data, size_t *out_size);
void fiction_epub_rich_free_page_image(uint8_t *data);

#ifdef __cplusplus
}
#endif

#endif /* LXJ_FICTION_EPUB_RICH_ADAPTER_H */
