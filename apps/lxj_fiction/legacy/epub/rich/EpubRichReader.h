#pragma once

#include <cstdint>
#include <string>

class Epub;

namespace fiction_epub_rich {

class Renderer;
class RubbishHtmlParser;

struct RichReadState {
  std::string path;
  uint16_t current_section = 0;
  uint16_t current_page = 0;
  uint16_t pages_in_current_section = 0;
};

class EpubRichReader {
private:
  RichReadState &state_;
  ::Epub *epub_ = nullptr;
  Renderer *renderer_ = nullptr;
  RubbishHtmlParser *parser_ = nullptr;

  void clear_parser();
  bool parse_and_layout_current_section();

public:
  EpubRichReader(RichReadState &state, Renderer *renderer);
  ~EpubRichReader();

  bool load();
  bool render();
  bool next();
  bool prev();
  bool jump_to(uint16_t section, uint16_t page);
  void invalidate_layout();

  const RichReadState &state() const { return state_; }
  int get_spine_count() const;
  const std::string &get_title() const;
  int get_toc_count() const;
  std::string get_toc_title(int toc_index) const;
  int get_section_for_toc(int toc_index) const;
  std::string get_current_page_text(int max_chars = 0) const;
  bool read_current_page_image(uint8_t **out_data, size_t *out_size) const;
};

}  // namespace fiction_epub_rich
