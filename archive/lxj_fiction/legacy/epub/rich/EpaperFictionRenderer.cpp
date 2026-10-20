#include "EpaperFictionRenderer.h"

#include <algorithm>
#include <cstring>

#ifndef UNIT_TEST
#include <esp_log.h>
#else
#define ESP_LOGW(args...)
#endif

#include "epaper_port.h"

namespace fiction_epub_rich {

uint8_t EpaperFictionRenderer::to_paint_color(uint8_t color) const {
  return color > 127 ? WHITE : BLACK;
}

int EpaperFictionRenderer::sx(int x) const {
  return content_x_ + x;
}

int EpaperFictionRenderer::sy(int y) const {
  return content_y_ + y;
}

void EpaperFictionRenderer::configure(cFONT *font,
                                      int content_x,
                                      int content_y,
                                      int content_width,
                                      int content_height,
                                      int line_height) {
  if (font) {
    font_ = font;
  }

  content_x_ = std::max(0, content_x);
  content_y_ = std::max(0, content_y);
  content_width_ = std::max(1, content_width);
  content_height_ = std::max(1, content_height);
  line_height_ = std::max(1, line_height);

  space_width_ = std::max(4, static_cast<int>(font_->Width_EN / 2));
}

void EpaperFictionRenderer::begin(uint8_t *buffer) {
  buffer_ = buffer;
  if (!buffer_) {
    return;
  }

  Paint_NewImage(buffer_, EPD_WIDTH, EPD_HEIGHT, 270, WHITE);
  Paint_SelectImage(buffer_);
  Paint_Clear(WHITE);
}

void EpaperFictionRenderer::draw_pixel(int x, int y, uint8_t color) {
  if (!buffer_) {
    return;
  }
  Paint_SelectImage(buffer_);
  Paint_SetPixel(sx(x), sy(y), to_paint_color(color));
}

int EpaperFictionRenderer::get_text_width(const char *text, bool bold, bool italic) {
  (void)bold;
  (void)italic;

  if (!text || !font_) {
    return 0;
  }

  int width = 0;
  size_t pos = 0;
  size_t len = strlen(text);

  while (pos < len) {
    unsigned char c = static_cast<unsigned char>(text[pos]);
    if (c < 0x80) {
      width += font_->Width_EN;
      pos += 1;
      continue;
    }

    int char_len = Get_UTF8_Char_Length(c);
    if (char_len <= 0) {
      char_len = 1;
    }
    width += font_->Width_CH;
    pos += static_cast<size_t>(char_len);
  }

  return width;
}

void EpaperFictionRenderer::draw_text(int x, int y, const char *text, bool bold, bool italic) {
  (void)bold;
  (void)italic;

  if (!buffer_ || !text || !font_) {
    return;
  }

  Paint_SelectImage(buffer_);
  Paint_DrawString_CN(sx(x), sy(y), text, font_, WHITE, BLACK);
}

void EpaperFictionRenderer::draw_rect(int x, int y, int width, int height, uint8_t color) {
  if (!buffer_ || width <= 0 || height <= 0) {
    return;
  }

  Paint_SelectImage(buffer_);
  Paint_DrawRectangle(sx(x), sy(y), sx(x + width), sy(y + height), to_paint_color(color), DOT_PIXEL_1X1, DRAW_FILL_EMPTY);
}

void EpaperFictionRenderer::draw_triangle(int x0,
                                          int y0,
                                          int x1,
                                          int y1,
                                          int x2,
                                          int y2,
                                          uint8_t color) {
  if (!buffer_) {
    return;
  }

  Paint_SelectImage(buffer_);
  UWORD c = to_paint_color(color);
  Paint_DrawLine(sx(x0), sy(y0), sx(x1), sy(y1), c, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
  Paint_DrawLine(sx(x1), sy(y1), sx(x2), sy(y2), c, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
  Paint_DrawLine(sx(x2), sy(y2), sx(x0), sy(y0), c, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
}

void EpaperFictionRenderer::draw_circle(int x, int y, int r, uint8_t color) {
  if (!buffer_ || r <= 0) {
    return;
  }

  Paint_SelectImage(buffer_);
  Paint_DrawCircle(sx(x), sy(y), r, to_paint_color(color), DOT_PIXEL_1X1, DRAW_FILL_EMPTY);
}

void EpaperFictionRenderer::fill_triangle(int x0,
                                          int y0,
                                          int x1,
                                          int y1,
                                          int x2,
                                          int y2,
                                          uint8_t color) {
  if (!buffer_) {
    return;
  }

  auto edge = [](int ax, int ay, int bx, int by, int px, int py) {
    return (px - ax) * (by - ay) - (py - ay) * (bx - ax);
  };

  int min_x = std::min({x0, x1, x2});
  int max_x = std::max({x0, x1, x2});
  int min_y = std::min({y0, y1, y2});
  int max_y = std::max({y0, y1, y2});

  Paint_SelectImage(buffer_);
  UWORD c = to_paint_color(color);

  for (int py = min_y; py <= max_y; ++py) {
    for (int px = min_x; px <= max_x; ++px) {
      int w0 = edge(x1, y1, x2, y2, px, py);
      int w1 = edge(x2, y2, x0, y0, px, py);
      int w2 = edge(x0, y0, x1, y1, px, py);
      bool has_neg = (w0 < 0) || (w1 < 0) || (w2 < 0);
      bool has_pos = (w0 > 0) || (w1 > 0) || (w2 > 0);
      if (!(has_neg && has_pos)) {
        Paint_SetPixel(sx(px), sy(py), c);
      }
    }
  }
}

void EpaperFictionRenderer::fill_rect(int x, int y, int width, int height, uint8_t color) {
  if (!buffer_ || width <= 0 || height <= 0) {
    return;
  }

  Paint_SelectImage(buffer_);
  Paint_DrawRectangle(sx(x), sy(y), sx(x + width), sy(y + height), to_paint_color(color), DOT_PIXEL_1X1, DRAW_FILL_FULL);
}

void EpaperFictionRenderer::fill_circle(int x, int y, int r, uint8_t color) {
  if (!buffer_ || r <= 0) {
    return;
  }

  Paint_SelectImage(buffer_);
  Paint_DrawCircle(sx(x), sy(y), r, to_paint_color(color), DOT_PIXEL_1X1, DRAW_FILL_FULL);
}

void EpaperFictionRenderer::needs_gray(uint8_t color) {
  (void)color;
}

bool EpaperFictionRenderer::has_gray() {
  return false;
}

void EpaperFictionRenderer::show_busy() {
}

void EpaperFictionRenderer::show_img(int x, int y, int width, int height, const uint8_t *img_buffer) {
  (void)x;
  (void)y;
  (void)width;
  (void)height;
  (void)img_buffer;
}

void EpaperFictionRenderer::clear_screen() {
  if (!buffer_) {
    return;
  }

  Paint_SelectImage(buffer_);
  Paint_Clear(WHITE);
}

int EpaperFictionRenderer::get_page_width() {
  return content_width_;
}

int EpaperFictionRenderer::get_page_height() {
  return content_height_;
}

int EpaperFictionRenderer::get_space_width() {
  return space_width_;
}

int EpaperFictionRenderer::get_line_height() {
  return line_height_;
}

}  // namespace fiction_epub_rich
