#include "epub_converter.h"

#include <cerrno>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <string>

#include <sys/stat.h>
#include <unistd.h>

#include <esp_log.h>

#include "Epub.h"
#include "htmlEntities.h"

namespace fiction_epub {

static const char* TAG = "EPUB_CONVERT";
static const char* kBookmarkDir = "/sdcard/bookmarks";
static const char* kCacheDir = "/sdcard/bookmarks/epub_cache";

static bool ensure_directory(const char* path)
{
    if (!path || path[0] == '\0') {
        return false;
    }

    struct stat st;
    if (stat(path, &st) == 0) {
        if (S_ISDIR(st.st_mode)) {
            return true;
        }
        ESP_LOGE(TAG, "%s exists but is not a directory", path);
        return false;
    }

    if (mkdir(path, 0775) == 0 || errno == EEXIST) {
        return true;
    }

    ESP_LOGE(TAG, "Failed to create directory %s, errno=%d", path, errno);
    return false;
}

static std::string to_lower(std::string s)
{
    for (char& c : s) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}

static std::string basename_no_ext(const char* path)
{
    const char* base = strrchr(path, '/');
    base = base ? base + 1 : path;
    std::string name = base ? std::string(base) : std::string();
    if (name.empty()) {
        return "book";
    }

    std::string lower = to_lower(name);
    if (lower.size() > 5 && lower.substr(lower.size() - 5) == ".epub") {
        name.erase(name.size() - 5);
    }

    if (name.empty()) {
        return "book";
    }
    return name;
}

static std::string sanitize_filename(const std::string& src)
{
    std::string out;
    out.reserve(src.size());

    for (char c : src) {
        unsigned char uc = static_cast<unsigned char>(c);
        if (std::isalnum(uc) || c == '-' || c == '_' || c == '.') {
            out.push_back(c);
        } else {
            out.push_back('_');
        }
    }

    if (out.empty()) {
        out = "book";
    }
    return out;
}

static bool file_is_up_to_date(const char* source_path, const char* cache_path)
{
    struct stat source_st;
    struct stat cache_st;

    if (stat(source_path, &source_st) != 0) {
        return false;
    }
    if (stat(cache_path, &cache_st) != 0) {
        return false;
    }
    if (cache_st.st_size <= 0) {
        return false;
    }

    return cache_st.st_mtime >= source_st.st_mtime;
}

static bool is_block_tag(const std::string& tag_name)
{
    return tag_name == "p" || tag_name == "div" || tag_name == "br" ||
           tag_name == "li" || tag_name == "tr" || tag_name == "hr" ||
           tag_name == "h1" || tag_name == "h2" || tag_name == "h3" ||
           tag_name == "h4" || tag_name == "h5" || tag_name == "h6" ||
           tag_name == "blockquote" || tag_name == "section" ||
           tag_name == "article" || tag_name == "header" || tag_name == "footer";
}

static std::string normalize_text(const std::string& text)
{
    std::string out;
    out.reserve(text.size());

    bool prev_space = false;
    int newline_run = 0;

    for (char c : text) {
        unsigned char uc = static_cast<unsigned char>(c);

        if (c == '\r') {
            continue;
        }

        if (c == '\n') {
            while (!out.empty() && out.back() == ' ') {
                out.pop_back();
            }

            if (out.empty()) {
                continue;
            }

            if (newline_run >= 2) {
                continue;
            }

            out.push_back('\n');
            newline_run++;
            prev_space = false;
            continue;
        }

        if (std::isspace(uc)) {
            if (out.empty() || out.back() == '\n' || prev_space) {
                continue;
            }
            out.push_back(' ');
            prev_space = true;
            newline_run = 0;
            continue;
        }

        out.push_back(c);
        prev_space = false;
        newline_run = 0;
    }

    while (!out.empty() && (out.back() == ' ' || out.back() == '\n')) {
        out.pop_back();
    }

    return out;
}

static std::string html_to_text(const std::string& html)
{
    std::string extracted;
    extracted.reserve(html.size());

    bool skip_script = false;
    bool skip_style = false;

    for (size_t i = 0; i < html.size(); ++i) {
        char c = html[i];

        if (c == '<') {
            size_t gt = html.find('>', i + 1);
            if (gt == std::string::npos) {
                break;
            }

            std::string raw_tag = html.substr(i + 1, gt - (i + 1));
            size_t start = 0;
            while (start < raw_tag.size() && std::isspace(static_cast<unsigned char>(raw_tag[start]))) {
                ++start;
            }
            size_t end = raw_tag.size();
            while (end > start && std::isspace(static_cast<unsigned char>(raw_tag[end - 1]))) {
                --end;
            }

            std::string tag = to_lower(raw_tag.substr(start, end - start));
            bool closing_tag = false;
            if (!tag.empty() && tag[0] == '/') {
                closing_tag = true;
                tag.erase(0, 1);
                while (!tag.empty() && std::isspace(static_cast<unsigned char>(tag[0]))) {
                    tag.erase(0, 1);
                }
            }
            if (!tag.empty() && tag.back() == '/') {
                tag.pop_back();
            }

            size_t space_pos = tag.find_first_of(" \t\r\n");
            std::string tag_name = (space_pos == std::string::npos) ? tag : tag.substr(0, space_pos);

            if (skip_script) {
                if (closing_tag && tag_name == "script") {
                    skip_script = false;
                }
                i = gt;
                continue;
            }
            if (skip_style) {
                if (closing_tag && tag_name == "style") {
                    skip_style = false;
                }
                i = gt;
                continue;
            }

            if (!closing_tag && tag_name == "script") {
                skip_script = true;
                i = gt;
                continue;
            }
            if (!closing_tag && tag_name == "style") {
                skip_style = true;
                i = gt;
                continue;
            }

            if (is_block_tag(tag_name) || (closing_tag && is_block_tag(tag_name))) {
                extracted.push_back('\n');
            } else if (tag_name == "td") {
                extracted.push_back(' ');
            }

            i = gt;
            continue;
        }

        if (skip_script || skip_style) {
            continue;
        }

        extracted.push_back(c);
    }

    std::string decoded = replace_html_entities(extracted);
    return normalize_text(decoded);
}

static bool write_string(FILE* fp, const std::string& text)
{
    if (text.empty()) {
        return true;
    }
    return fwrite(text.data(), 1, text.size(), fp) == text.size();
}

bool ConvertEpubToText(const char* epub_path,
                       const char* output_txt_path,
                       std::string* error_message)
{
    if (!epub_path || !output_txt_path) {
        if (error_message) {
            *error_message = "invalid path";
        }
        return false;
    }

    Epub epub(epub_path);
    if (!epub.load()) {
        if (error_message) {
            *error_message = "failed to parse epub";
        }
        return false;
    }

    FILE* fp = fopen(output_txt_path, "wb");
    if (!fp) {
        if (error_message) {
            *error_message = "failed to open output file";
        }
        ESP_LOGE(TAG, "Open output failed: %s", output_txt_path);
        return false;
    }

    size_t sections_written = 0;

    const std::string& title = epub.get_title();
    if (!title.empty()) {
        write_string(fp, title);
        write_string(fp, "\n\n");
    }

    const int spine_count = epub.get_spine_items_count();
    for (int i = 0; i < spine_count; ++i) {
        std::string section_href = epub.get_spine_item(i);
        if (section_href.empty()) {
            continue;
        }

        size_t data_size = 0;
        uint8_t* data = epub.get_item_contents(section_href, &data_size);
        if (!data || data_size == 0) {
            if (data) {
                free(data);
            }
            ESP_LOGW(TAG, "Skip empty section %d: %s", i, section_href.c_str());
            continue;
        }

        std::string html(reinterpret_cast<const char*>(data), data_size);
        free(data);

        std::string text = html_to_text(html);
        if (text.empty()) {
            continue;
        }

        if (sections_written > 0) {
            write_string(fp, "\n\n");
        }

        if (!write_string(fp, text) || !write_string(fp, "\n")) {
            fclose(fp);
            if (error_message) {
                *error_message = "failed to write output";
            }
            ESP_LOGE(TAG, "Write output failed: %s", output_txt_path);
            unlink(output_txt_path);
            return false;
        }

        sections_written++;
    }

    fclose(fp);

    if (sections_written == 0) {
        if (error_message) {
            *error_message = "epub has no readable text section";
        }
        unlink(output_txt_path);
        ESP_LOGE(TAG, "No readable text section in epub: %s", epub_path);
        return false;
    }

    ESP_LOGI(TAG, "EPUB converted: %s -> %s, sections=%d", epub_path, output_txt_path, (int)sections_written);
    return true;
}

bool PrepareEpubTextCache(const char* epub_path,
                          char* out_cache_path,
                          size_t out_cache_path_len,
                          std::string* error_message)
{
    if (!epub_path || !out_cache_path || out_cache_path_len == 0) {
        if (error_message) {
            *error_message = "invalid arguments";
        }
        return false;
    }

    if (!ensure_directory(kBookmarkDir) || !ensure_directory(kCacheDir)) {
        if (error_message) {
            *error_message = "failed to create cache directory";
        }
        return false;
    }

    const std::string stem = basename_no_ext(epub_path);
    const std::string file_name = sanitize_filename(stem) + ".txt";

    int n = snprintf(out_cache_path, out_cache_path_len, "%s/%s", kCacheDir, file_name.c_str());
    if (n < 0 || static_cast<size_t>(n) >= out_cache_path_len) {
        if (error_message) {
            *error_message = "cache path too long";
        }
        return false;
    }

    if (file_is_up_to_date(epub_path, out_cache_path)) {
        ESP_LOGI(TAG, "Reuse epub cache: %s", out_cache_path);
        return true;
    }

    ESP_LOGI(TAG, "Build epub cache: %s", out_cache_path);
    if (!ConvertEpubToText(epub_path, out_cache_path, error_message)) {
        return false;
    }
    return true;
}

}  // namespace fiction_epub
