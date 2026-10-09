#include "fiction_epub_rich_adapter.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

#include "legacy/epub/rich/EpubRichReader.h"
#include "legacy/epub/rich/Renderer.h"

namespace fiction_epub_rich {

/* The parser asks the renderer for geometry while laying out text. Drawing is
 * intentionally a no-op: the current application renders the resulting page
 * text through LVGL, so the legacy e-paper owner never enters the build. */
class TextLayoutRenderer final : public Renderer {
public:
    void draw_pixel(int, int, uint8_t) override {}
    int get_text_width(const char *text, bool bold, bool italic) override
    {
        (void)bold;
        (void)italic;
        return text ? static_cast<int>(std::strlen(text) * 8U) : 0;
    }
    void draw_text(int, int, const char *, bool, bool) override {}
    void draw_rect(int, int, int, int, uint8_t) override {}
    void draw_triangle(int, int, int, int, int, int, uint8_t) override {}
    void draw_circle(int, int, int, uint8_t) override {}
    void fill_triangle(int, int, int, int, int, int, uint8_t) override {}
    void fill_rect(int, int, int, int, uint8_t) override {}
    void fill_circle(int, int, int, uint8_t) override {}
    void needs_gray(uint8_t) override {}
    bool has_gray() override { return false; }
    void show_busy() override {}
    void show_img(int, int, int, int, const uint8_t *) override {}
    void clear_screen() override {}
    int get_page_width() override { return 700; }
    int get_page_height() override { return 340; }
    int get_space_width() override { return 8; }
    int get_line_height() override { return 22; }
};

void Renderer::draw_image(const std::string &, const uint8_t *, size_t,
                          int, int, int, int) {}

bool Renderer::get_image_size(const std::string &, const uint8_t *, size_t,
                              int *, int *)
{
    return false;
}

void Renderer::draw_text_box(const std::string &, int, int, int, int,
                             bool, bool) {}

}  // namespace fiction_epub_rich

namespace {

fiction_epub_rich::TextLayoutRenderer g_renderer;
fiction_epub_rich::RichReadState g_state;
fiction_epub_rich::EpubRichReader *g_reader = nullptr;

uint32_t path_hash(const char *path)
{
    uint32_t hash = 2166136261U;
    for (const unsigned char *p = reinterpret_cast<const unsigned char *>(path);
         p && *p; ++p) {
        hash ^= *p;
        hash *= 16777619U;
    }
    return hash;
}

void progress_path(const char *epub_path, char *out, size_t out_len)
{
    std::snprintf(out, out_len, "/sdcard/bookmarks/epub_progress/%08lx.state",
                  static_cast<unsigned long>(path_hash(epub_path)));
}

void load_progress()
{
    char path[128];
    progress_path(g_state.path.c_str(), path, sizeof(path));
    FILE *file = std::fopen(path, "r");
    if (!file) return;

    unsigned section = 0;
    unsigned page = 0;
    if (std::fscanf(file, "%u %u", &section, &page) == 2) {
        g_state.current_section = static_cast<uint16_t>(section);
        g_state.current_page = static_cast<uint16_t>(page);
    }
    std::fclose(file);
}

void save_progress()
{
    if (g_state.path.empty()) return;
    (void)::mkdir("/sdcard/bookmarks", 0775);
    (void)::mkdir("/sdcard/bookmarks/epub_progress", 0775);

    char path[128];
    char temp_path[136];
    progress_path(g_state.path.c_str(), path, sizeof(path));
    std::snprintf(temp_path, sizeof(temp_path), "%s.tmp", path);

    FILE *file = std::fopen(temp_path, "w");
    if (!file) return;
    std::fprintf(file, "%u %u\n", static_cast<unsigned>(g_state.current_section),
                 static_cast<unsigned>(g_state.current_page));
    if (std::fclose(file) == 0) {
        (void)::rename(temp_path, path);
    } else {
        (void)::unlink(temp_path);
    }
}

void copy_text(const std::string &value, char *out, size_t out_len)
{
    if (!out || out_len == 0) return;
    size_t count = std::min(value.size(), out_len - 1U);
    std::memcpy(out, value.data(), count);
    out[count] = '\0';
}

bool ensure_page_loaded()
{
    return g_reader && g_reader->jump_to(g_state.current_section,
                                         g_state.current_page);
}

}  // namespace

extern "C" bool fiction_epub_rich_open(const char *epub_path)
{
    fiction_epub_rich_close();
    if (!epub_path || !epub_path[0]) return false;
    g_state = {};
    g_state.path = epub_path;
    load_progress();
    g_reader = new fiction_epub_rich::EpubRichReader(g_state, &g_renderer);
    if (!g_reader->load()) {
        fiction_epub_rich_close();
        return false;
    }
    return ensure_page_loaded();
}

extern "C" void fiction_epub_rich_close(void)
{
    delete g_reader;
    g_reader = nullptr;
    g_state = {};
}

extern "C" bool fiction_epub_rich_read_page(char *out_text, size_t out_text_len)
{
    if (!out_text || out_text_len == 0 || !ensure_page_loaded()) return false;
    copy_text(g_reader->get_current_page_text(), out_text, out_text_len);
    return out_text[0] != '\0';
}

extern "C" bool fiction_epub_rich_next(void)
{
    bool moved = g_reader && g_reader->next();
    if (moved) save_progress();
    return moved;
}

extern "C" bool fiction_epub_rich_prev(void)
{
    bool moved = g_reader && g_reader->prev();
    if (moved) save_progress();
    return moved;
}

extern "C" int fiction_epub_rich_toc_count(void)
{
    return g_reader ? g_reader->get_toc_count() : 0;
}

extern "C" bool fiction_epub_rich_toc_title(int index, char *out_title,
                                              size_t out_title_len)
{
    if (!g_reader || index < 0 || index >= g_reader->get_toc_count() ||
        !out_title || out_title_len == 0) return false;
    copy_text(g_reader->get_toc_title(index), out_title, out_title_len);
    return out_title[0] != '\0';
}

extern "C" bool fiction_epub_rich_jump_to_toc(int index)
{
    if (!g_reader || index < 0 || index >= g_reader->get_toc_count()) return false;
    int section = g_reader->get_section_for_toc(index);
    if (section < 0) return false;
    g_state.current_section = static_cast<uint16_t>(section);
    g_state.current_page = 0;
    bool moved = ensure_page_loaded();
    if (moved) save_progress();
    return moved;
}

extern "C" bool fiction_epub_rich_read_page_image(uint8_t **out_data,
                                                   size_t *out_size)
{
    if (!g_reader || !ensure_page_loaded()) return false;
    return g_reader->read_current_page_image(out_data, out_size);
}

extern "C" void fiction_epub_rich_free_page_image(uint8_t *data)
{
    std::free(data);
}
