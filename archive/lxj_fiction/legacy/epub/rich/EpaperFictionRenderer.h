#pragma once

#include <cstdint>

#include "Renderer.h"
#include "GUI_Paint.h"
#include "font.h"

namespace fiction_epub_rich {

class EpaperFictionRenderer : public Renderer {
private:
  uint8_t *buffer_ = nullptr;
  cFONT *font_ = &Font18_UTF8;

  int content_x_ = 10;
  int content_y_ = 70;
  int content_width_ = 460;
  int content_height_ = 700;
  int line_height_ = 36;
  int space_width_ = 8;

  uint8_t to_paint_color(uint8_t color) const;
  int sx(int x) const;
  int sy(int y) const;

public:
  EpaperFictionRenderer() = default;
  ~EpaperFictionRenderer() override = default;

  void configure(cFONT *font,
                 int content_x,
                 int content_y,
                 int content_width,
                 int content_height,
                 int line_height);

  void begin(uint8_t *buffer);

  void draw_pixel(int x, int y, uint8_t color) override;
  int get_text_width(const char *text, bool bold = false, bool italic = false) override;
  void draw_text(int x, int y, const char *text, bool bold = false, bool italic = false) override;

  void draw_rect(int x, int y, int width, int height, uint8_t color = 0) override;
  void draw_triangle(int x0,
                     int y0,
                     int x1,
                     int y1,
                     int x2,
                     int y2,
                     uint8_t color) override;
  void draw_circle(int x, int y, int r, uint8_t color = 0) override;

  void fill_triangle(int x0,
                     int y0,
                     int x1,
                     int y1,
                     int x2,
                     int y2,
                     uint8_t color) override;
  void fill_rect(int x, int y, int width, int height, uint8_t color = 0) override;
  void fill_circle(int x, int y, int r, uint8_t color = 0) override;

  void needs_gray(uint8_t color) override;
  bool has_gray() override;
  void show_busy() override;
  void show_img(int x, int y, int width, int height, const uint8_t *img_buffer) override;
  void clear_screen() override;

  int get_page_width() override;
  int get_page_height() override;
  int get_space_width() override;
  int get_line_height() override;
};

}  // namespace fiction_epub_rich

