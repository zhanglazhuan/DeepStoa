#pragma once

#include <algorithm>
#include <cstdlib>
#include <string>

#include "../../Epub.h"
#include "../Renderer.h"
#include "Block.h"

namespace fiction_epub_rich {

class ImageBlock : public Block {
public:
  std::string m_src;
  int y_pos = 0;
  int x_pos = 0;
  int width = 0;
  int height = 0;

  explicit ImageBlock(std::string src) : m_src(std::move(src)) {}

  bool isEmpty() override { return m_src.empty(); }

  void layout(Renderer *renderer, ::Epub *epub, int max_width = -1) override {
    if (!renderer || !epub) {
      return;
    }

    size_t image_data_size = 0;
    uint8_t *image_data = epub->get_item_contents(m_src, &image_data_size);
    if (!image_data) {
      width = std::min(renderer->get_page_width(), 160);
      height = std::min(renderer->get_page_height(), 120);
    } else {
      renderer->get_image_size(m_src, image_data, image_data_size, &width, &height);
      free(image_data);
    }

    if (width <= 0 || height <= 0) {
      width = std::min(renderer->get_page_width(), 120);
      height = std::min(renderer->get_page_height(), 80);
    }

    int target_width = (max_width > 0) ? max_width : renderer->get_page_width();
    if (width > target_width || height > renderer->get_page_height()) {
      float scale = std::min(static_cast<float>(target_width) / static_cast<float>(width),
                             static_cast<float>(renderer->get_page_height()) / static_cast<float>(height));
      if (scale < 1.0f) {
        width = static_cast<int>(width * scale);
        height = static_cast<int>(height * scale);
      }
    }

    x_pos = (renderer->get_page_width() - width) / 2;
    if (x_pos < 0) {
      x_pos = 0;
    }
  }

  void render(Renderer *renderer, ::Epub *epub, int y) {
    if (!renderer || !epub) {
      return;
    }

    size_t image_data_size = 0;
    uint8_t *image_data = epub->get_item_contents(m_src, &image_data_size);
    renderer->fill_rect(x_pos, y, width, height, 255);
    renderer->flush_area(x_pos, y, width, height);

    if (image_data) {
      renderer->draw_image(m_src, image_data, image_data_size, x_pos, y, width, height);
      free(image_data);
    } else {
      renderer->draw_rect(x_pos, y, width, height, 0);
      renderer->draw_text(x_pos + 4, y + 4, "[missing image]");
    }
  }

  void dump() override { printf("ImageBlock: %s\n", m_src.c_str()); }

  BlockType getType() override { return BlockType::IMAGE_BLOCK; }
};

}  // namespace fiction_epub_rich
