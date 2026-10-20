#include "fiction_epub_adapter.h"

#include <cstdio>
#include <string>

#include "legacy/epub/epub_converter.h"

extern "C" bool fiction_epub_prepare_cache(const char *epub_path,
                                             char *out_cache_path,
                                             size_t out_cache_path_len,
                                             char *error_message,
                                             size_t error_message_len)
{
    if (error_message && error_message_len > 0) {
        error_message[0] = '\0';
    }

    std::string error;
    bool ok = fiction_epub::PrepareEpubTextCache(
        epub_path, out_cache_path, out_cache_path_len, &error);
    if (!ok && error_message && error_message_len > 0) {
        std::snprintf(error_message, error_message_len, "%s",
                      error.empty() ? "EPUB conversion failed" : error.c_str());
    }
    return ok;
}
