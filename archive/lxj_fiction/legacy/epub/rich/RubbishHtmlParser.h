#pragma once

#include <list>
#include <string>
#include <vector>

#include "../tinyxml2/tinyxml2.h"
#include "blocks/TextBlock.h"

class Epub;

namespace fiction_epub_rich {

class Page;
class Renderer;
class Block;

class RubbishHtmlParser : public tinyxml2::XMLVisitor {
private:
  bool is_bold = false;
  bool is_italic = false;

  std::list<Block *> blocks;
  TextBlock *current_text_block = nullptr;
  std::vector<Page *> pages;

  std::string base_path_;

  void start_new_text_block(BLOCK_STYLE style);

public:
  RubbishHtmlParser(const char *html, int length, const std::string &base_path);
  ~RubbishHtmlParser() override;

  bool VisitEnter(const tinyxml2::XMLElement &element,
                  const tinyxml2::XMLAttribute *firstAttribute) override;
  bool Visit(const tinyxml2::XMLText &text) override;
  bool VisitExit(const tinyxml2::XMLElement &element) override;

  void parse(const char *html, int length);
  void add_text(const char *text, bool is_bold, bool is_italic);
  void layout(Renderer *renderer, ::Epub *epub);

  int get_page_count() const { return static_cast<int>(pages.size()); }
  const std::list<Block *> &get_blocks() const { return blocks; }
  void render_page(int page_index, Renderer *renderer, ::Epub *epub);
  std::string get_page_plain_text(int page_index, int max_chars = 0) const;
  std::string get_page_image_src(int page_index, int image_index = 0) const;
};

}  // namespace fiction_epub_rich
