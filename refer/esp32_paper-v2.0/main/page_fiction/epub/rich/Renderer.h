#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace fiction_epub_rich {

class Renderer {
public:
  virtual ~Renderer() = default;

  virtual void draw_image(const std::string &filename,
                          const uint8_t *data,
                          size_t data_size,
                          int x,
                          int y,
                          int width,
                          int height);

  virtual bool get_image_size(const std::string &filename,
                              const uint8_t *data,
                              size_t data_size,
                              int *width,
                              int *height);

  virtual void draw_pixel(int x, int y, uint8_t color) = 0;
  virtual int get_text_width(const char *text, bool bold = false, bool italic = false) = 0;
  virtual void draw_text(int x, int y, const char *text, bool bold = false, bool italic = false) = 0;

  virtual void draw_text_box(const std::string &text,
                             int x,
                             int y,
                             int width,
                             int height,
                             bool bold = false,
                             bool italic = false);

  virtual void draw_rect(int x, int y, int width, int height, uint8_t color = 0) = 0;
  virtual void draw_triangle(int x0,
                             int y0,
                             int x1,
                             int y1,
                             int x2,
                             int y2,
                             uint8_t color) = 0;
  virtual void draw_circle(int x, int y, int r, uint8_t color = 0) = 0;

  virtual void fill_triangle(int x0,
                             int y0,
                             int x1,
                             int y1,
                             int x2,
                             int y2,
                             uint8_t color) = 0;
  virtual void fill_rect(int x, int y, int width, int height, uint8_t color = 0) = 0;
  virtual void fill_circle(int x, int y, int r, uint8_t color = 0) = 0;

  virtual void needs_gray(uint8_t color) = 0;
  virtual bool has_gray() = 0;
  virtual void show_busy() = 0;
  virtual void show_img(int x, int y, int width, int height, const uint8_t *img_buffer) = 0;
  virtual void clear_screen() = 0;
  virtual void flush_display() {}
  virtual void flush_area(int x, int y, int width, int height) {}

  virtual int get_page_width() = 0;
  virtual int get_page_height() = 0;
  virtual int get_space_width() = 0;
  virtual int get_line_height() = 0;

  uint8_t temperature = 20;
};

}  // namespace fiction_epub_rich

