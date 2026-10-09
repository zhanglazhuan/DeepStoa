#pragma once

#include <cstdint>
#include <vector>

#include "../../Epub.h"
#include "../Renderer.h"
#include "Block.h"

namespace fiction_epub_rich {

enum SPAN_STYLE {
  BOLD_SPAN = 1,
  ITALIC_SPAN = 2,
};

enum BLOCK_STYLE {
  JUSTIFIED = 0,
  LEFT_ALIGN = 1,
  CENTER_ALIGN = 2,
  RIGHT_ALIGN = 3,
};

class TextBlock : public Block {
private:
  std::vector<const char *> spans;
  std::vector<const char *> words;
  std::vector<uint16_t> word_widths;
  std::vector<uint16_t> word_xpos;
  std::vector<uint8_t> word_styles;
  std::vector<uint8_t> word_has_space_before;

  BLOCK_STYLE style;

public:
  std::vector<uint16_t> line_breaks;

  explicit TextBlock(BLOCK_STYLE style) : style(style) {}
  ~TextBlock() override;

  void add_span(const char *span, bool is_bold, bool is_italic);

  void set_style(BLOCK_STYLE s) { style = s; }
  BLOCK_STYLE get_style() { return style; }

  bool isEmpty() override { return spans.empty(); }
  bool is_empty() { return words.empty(); }

  void layout(Renderer *renderer, ::Epub *epub, int max_width = -1) override;
  void render(Renderer *renderer, int line_break_index, int x_pos, int y_pos);
  void dump() override;
  void append_line_plain_text(int line_break_index, std::string &out) const;

  BlockType getType() override { return BlockType::TEXT_BLOCK; }
};

}  // namespace fiction_epub_rich
