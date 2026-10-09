#pragma once

#include <cstddef>
#include <string>

namespace fiction_epub {

bool ConvertEpubToText(const char* epub_path,
                       const char* output_txt_path,
                       std::string* error_message = nullptr);

bool PrepareEpubTextCache(const char* epub_path,
                          char* out_cache_path,
                          size_t out_cache_path_len,
                          std::string* error_message = nullptr);

}  // namespace fiction_epub
