#pragma once

#include <vector>

#include "blocks/ImageBlock.h"
#include "blocks/TextBlock.h"

namespace fiction_epub_rich {

class PageElement {
public:
  int y_pos;

  explicit PageElement(int y_pos) : y_pos(y_pos) {}
  virtual ~PageElement() = default;
  virtual bool is_text_line() const { return false; }
  virtual bool is_image() const { return false; }
  virtual void render(Renderer *renderer, ::Epub *epub) = 0;
};

class PageLine : public PageElement {
public:
  TextBlock *block;
  int line_break_index;

  PageLine(TextBlock *block, int line_break_index, int y_pos)
      : PageElement(y_pos), block(block), line_break_index(line_break_index) {}

  bool is_text_line() const override { return true; }

  void render(Renderer *renderer, ::Epub *epub) override {
    (void)epub;
    if (!block || !renderer) {
      return;
    }
    block->render(renderer, line_break_index, 0, y_pos);
  }
};

class PageImage : public PageElement {
public:
  ImageBlock *block;

  PageImage(ImageBlock *block, int y_pos) : PageElement(y_pos), block(block) {}

  bool is_image() const override { return true; }

  void render(Renderer *renderer, ::Epub *epub) override {
    if (!block || !renderer || !epub) {
      return;
    }
    block->render(renderer, epub, y_pos);
  }
};

class Page {
public:
  std::vector<PageElement *> elements;

  void render(Renderer *renderer, ::Epub *epub) {
    for (auto *element : elements) {
      if (element) {
        element->render(renderer, epub);
      }
    }
  }

  ~Page() {
    for (auto *element : elements) {
      delete element;
    }
  }
};

}  // namespace fiction_epub_rich
