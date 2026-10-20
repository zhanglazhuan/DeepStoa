#include "Renderer.h"

#include <algorithm>
#include <cctype>
#include <climits>
#include <cstring>
#include <cstdlib>
#include <string>

#include "esp_jpeg_common.h"
#include "esp_jpeg_dec.h"

#ifndef LODEPNG_NO_COMPILE_CPP
#define LODEPNG_NO_COMPILE_CPP
#endif
#include "libs/lodepng/lodepng.h"

#ifndef UNIT_TEST
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#else
#define ESP_LOGW(args...)
#define ESP_LOGI(args...)
#define vTaskDelay(t)
#endif

namespace fiction_epub_rich {

namespace {

static const char *TAG = "EPUB_RENDER";

enum class ImageType {
  kUnknown,
  kJpeg,
  kPng,
};

bool has_suffix(const std::string &text, const char *suffix) {
  if (!suffix) {
    return false;
  }

  size_t suffix_len = strlen(suffix);
  if (text.size() < suffix_len) {
    return false;
  }

  size_t start = text.size() - suffix_len;
  for (size_t i = 0; i < suffix_len; ++i) {
    unsigned char lhs = static_cast<unsigned char>(text[start + i]);
    unsigned char rhs = static_cast<unsigned char>(suffix[i]);
    if (std::tolower(lhs) != std::tolower(rhs)) {
      return false;
    }
  }
  return true;
}

ImageType detect_image_type(const std::string &filename, const uint8_t *data, size_t data_size) {
  if (data && data_size >= 3 && data[0] == 0xFF && data[1] == 0xD8 && data[2] == 0xFF) {
    return ImageType::kJpeg;
  }
  if (data && data_size >= 8 && data[0] == 0x89 && data[1] == 0x50 && data[2] == 0x4E &&
      data[3] == 0x47 && data[4] == 0x0D && data[5] == 0x0A && data[6] == 0x1A &&
      data[7] == 0x0A) {
    return ImageType::kPng;
  }

  if (has_suffix(filename, ".jpg") || has_suffix(filename, ".jpeg")) {
    return ImageType::kJpeg;
  }
  if (has_suffix(filename, ".png")) {
    return ImageType::kPng;
  }
  return ImageType::kUnknown;
}

bool get_jpeg_size(const uint8_t *data, size_t data_size, int *width, int *height) {
  if (!data || data_size == 0 || !width || !height || data_size > static_cast<size_t>(INT_MAX)) {
    return false;
  }

  jpeg_dec_config_t config = DEFAULT_JPEG_DEC_CONFIG();
  config.output_type = JPEG_PIXEL_FORMAT_RGB888;
  config.rotate = JPEG_ROTATE_0D;

  jpeg_dec_handle_t decoder = nullptr;
  if (jpeg_dec_open(&config, &decoder) != JPEG_ERR_OK || !decoder) {
    return false;
  }

  jpeg_dec_io_t io = {};
  io.inbuf = const_cast<uint8_t *>(data);
  io.inbuf_len = static_cast<int>(data_size);

  jpeg_dec_header_info_t info = {};
  bool ok = (jpeg_dec_parse_header(decoder, &io, &info) == JPEG_ERR_OK && info.width > 0 &&
             info.height > 0);
  if (ok) {
    *width = static_cast<int>(info.width);
    *height = static_cast<int>(info.height);
  }

  jpeg_dec_close(decoder);
  return ok;
}

bool get_png_size(const uint8_t *data, size_t data_size, int *width, int *height) {
  if (!data || data_size == 0 || !width || !height) {
    return false;
  }

  unsigned image_width = 0;
  unsigned image_height = 0;

  LodePNGState state;
  lodepng_state_init(&state);
  unsigned error = lodepng_inspect(&image_width, &image_height, &state, data, data_size);
  lodepng_state_cleanup(&state);

  if (error != 0 || image_width == 0 || image_height == 0 || image_width > INT_MAX ||
      image_height > INT_MAX) {
    return false;
  }

  *width = static_cast<int>(image_width);
  *height = static_cast<int>(image_height);
  return true;
}

inline uint8_t rgb_to_gray(uint8_t r, uint8_t g, uint8_t b) {
  return static_cast<uint8_t>((r * 38 + g * 75 + b * 15) >> 7);
}

void blend_with_white(uint8_t &r, uint8_t &g, uint8_t &b, uint8_t a) {
  if (a == 255) {
    return;
  }
  if (a == 0) {
    r = 255;
    g = 255;
    b = 255;
    return;
  }

  uint16_t inv_a = static_cast<uint16_t>(255 - a);
  r = static_cast<uint8_t>((static_cast<uint16_t>(r) * a + 255U * inv_a) / 255U);
  g = static_cast<uint8_t>((static_cast<uint16_t>(g) * a + 255U * inv_a) / 255U);
  b = static_cast<uint8_t>((static_cast<uint16_t>(b) * a + 255U * inv_a) / 255U);
}

bool draw_jpeg(Renderer *renderer,
               const uint8_t *data,
               size_t data_size,
               int x,
               int y,
               int width,
               int height) {
  if (!renderer || !data || data_size == 0 || data_size > static_cast<size_t>(INT_MAX) || width <= 0 ||
      height <= 0) {
    return false;
  }

  jpeg_dec_config_t config = DEFAULT_JPEG_DEC_CONFIG();
  config.output_type = JPEG_PIXEL_FORMAT_RGB888;
  config.rotate = JPEG_ROTATE_0D;

  jpeg_dec_handle_t decoder = nullptr;
  if (jpeg_dec_open(&config, &decoder) != JPEG_ERR_OK || !decoder) {
    ESP_LOGW(TAG, "jpeg_dec_open failed");
    return false;
  }

  jpeg_dec_io_t io = {};
  io.inbuf = const_cast<uint8_t *>(data);
  io.inbuf_len = static_cast<int>(data_size);

  jpeg_dec_header_info_t info = {};
  if (jpeg_dec_parse_header(decoder, &io, &info) != JPEG_ERR_OK || info.width == 0 || info.height == 0) {
    ESP_LOGW(TAG, "jpeg_dec_parse_header failed");
    jpeg_dec_close(decoder);
    return false;
  }

  int outbuf_len = 0;
  if (jpeg_dec_get_outbuf_len(decoder, &outbuf_len) != JPEG_ERR_OK || outbuf_len <= 0) {
    ESP_LOGW(TAG, "jpeg_dec_get_outbuf_len failed");
    jpeg_dec_close(decoder);
    return false;
  }

  uint8_t *outbuf = static_cast<uint8_t *>(jpeg_calloc_align(static_cast<size_t>(outbuf_len), 16));
  if (!outbuf) {
    ESP_LOGW(TAG, "jpeg output buffer alloc failed");
    jpeg_dec_close(decoder);
    return false;
  }

  io.outbuf = outbuf;
  if (jpeg_dec_process(decoder, &io) != JPEG_ERR_OK || io.out_size <= 0) {
    ESP_LOGW(TAG, "jpeg_dec_process failed");
    jpeg_free_align(outbuf);
    jpeg_dec_close(decoder);
    return false;
  }

  int src_w = static_cast<int>(info.width);
  int src_h = static_cast<int>(info.height);
  int stride = src_w * 3;

  for (int dy = 0; dy < height; ++dy) {
    int sy = (dy * src_h) / height;
    const uint8_t *src_row = outbuf + sy * stride;
    for (int dx = 0; dx < width; ++dx) {
      int sx = (dx * src_w) / width;
      const uint8_t *pixel = src_row + sx * 3;
      renderer->draw_pixel(x + dx, y + dy, rgb_to_gray(pixel[0], pixel[1], pixel[2]));
    }
    if ((dy & 0x0F) == 0) {
      vTaskDelay(1);
    }
  }

  jpeg_free_align(outbuf);
  jpeg_dec_close(decoder);
  return true;
}

bool draw_png(Renderer *renderer,
              const uint8_t *data,
              size_t data_size,
              int x,
              int y,
              int width,
              int height) {
  if (!renderer || !data || data_size == 0 || width <= 0 || height <= 0) {
    return false;
  }

  unsigned src_w = 0;
  unsigned src_h = 0;
  unsigned char *rgba = nullptr;
  unsigned error = lodepng_decode32(&rgba, &src_w, &src_h, data, data_size);
  if (error != 0 || !rgba || src_w == 0 || src_h == 0) {
    if (rgba) {
      free(rgba);
    }
    ESP_LOGW(TAG, "lodepng_decode32 failed: %u", error);
    return false;
  }

  for (int dy = 0; dy < height; ++dy) {
    unsigned sy = static_cast<unsigned>((static_cast<uint64_t>(dy) * src_h) / static_cast<unsigned>(height));
    const uint8_t *src_row = rgba + static_cast<size_t>(sy) * src_w * 4U;
    for (int dx = 0; dx < width; ++dx) {
      unsigned sx = static_cast<unsigned>((static_cast<uint64_t>(dx) * src_w) / static_cast<unsigned>(width));
      const uint8_t *pixel = src_row + static_cast<size_t>(sx) * 4U;
      uint8_t r = pixel[0];
      uint8_t g = pixel[1];
      uint8_t b = pixel[2];
      blend_with_white(r, g, b, pixel[3]);
      renderer->draw_pixel(x + dx, y + dy, rgb_to_gray(r, g, b));
    }
    if ((dy & 0x0F) == 0) {
      vTaskDelay(1);
    }
  }

  free(rgba);
  return true;
}

}  // namespace

void Renderer::draw_image(const std::string &filename,
                          const uint8_t *data,
                          size_t data_size,
                          int x,
                          int y,
                          int width,
                          int height) {
  if (width <= 0 || height <= 0 || !data || data_size == 0) {
    return;
  }

  bool drawn = false;
  switch (detect_image_type(filename, data, data_size)) {
    case ImageType::kJpeg:
      drawn = draw_jpeg(this, data, data_size, x, y, width, height);
      break;
    case ImageType::kPng:
      drawn = draw_png(this, data, data_size, x, y, width, height);
      break;
    default:
      drawn = false;
      break;
  }

  if (!drawn) {
    ESP_LOGW(TAG, "Fallback image placeholder: %s (bytes=%u)", filename.c_str(),
             static_cast<unsigned>(data_size));
    draw_rect(x, y, width, height, 0);
    draw_rect(x + 2, y + 2, std::max(0, width - 4), std::max(0, height - 4), 0);
    draw_text(x + 6, y + 6, "[image]");
  }
}

bool Renderer::get_image_size(const std::string &filename,
                              const uint8_t *data,
                              size_t data_size,
                              int *width,
                              int *height) {
  if (!width || !height) {
    return false;
  }

  bool parsed = false;
  switch (detect_image_type(filename, data, data_size)) {
    case ImageType::kJpeg:
      parsed = get_jpeg_size(data, data_size, width, height);
      break;
    case ImageType::kPng:
      parsed = get_png_size(data, data_size, width, height);
      break;
    default:
      parsed = false;
      break;
  }

  if (parsed && *width > 0 && *height > 0) {
    return true;
  }

  int side = std::min(get_page_width(), get_page_height());
  if (side <= 0) {
    side = 120;
  }

  *width = side;
  *height = side;
  return false;
}

void Renderer::draw_text_box(const std::string &text,
                             int x,
                             int y,
                             int width,
                             int height,
                             bool bold,
                             bool italic) {
  int length = static_cast<int>(text.length());
  int start = 0;
  int end = 1;
  int ypos = 0;

  while (start < length && ypos + get_line_height() < height) {
    while (end < length &&
           get_text_width(text.substr(start, end - start).c_str(), bold, italic) < width) {
      end++;
    }
    if (get_text_width(text.substr(start, end - start).c_str(), bold, italic) > width) {
      end--;
    }
    draw_text(x, y + ypos, text.substr(start, end - start).c_str(), bold, italic);
    ypos += get_line_height();
    start = end;
    end = start + 1;
  }
}

}  // namespace fiction_epub_rich
