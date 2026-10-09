#include "RubbishHtmlParser.h"

#ifndef UNIT_TEST
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#else
#define ESP_LOGE(args...)
#define ESP_LOGI(args...)
#define vTaskDelay(t)
#endif

#include <cstring>
#include <string>
#include <stdexcept>

#include "../Epub.h"
#include "../htmlEntities.h"
#include "Page.h"
#include "Renderer.h"
#include "blocks/Block.h"
#include "blocks/ImageBlock.h"
#include "blocks/TextBlock.h"

namespace fiction_epub_rich {

static const char *TAG = "EPUB_HTML";

static const char *kHeaderTags[] = {"h1", "h2", "h3", "h4", "h5", "h6"};
static const char *kBlockTags[] = {"p", "li", "div", "br", "blockquote"};
static const char *kBoldTags[] = {"b", "strong"};
static const char *kItalicTags[] = {"i", "em"};
static const char *kImageTags[] = {"img"};
static const char *kSkipTags[] = {"head", "table", "script", "style", "svg"};

static bool matches_tag(const char *tag_name, const char *const *possible_tags, int count) {
  if (!tag_name) {
    return false;
  }

  for (int i = 0; i < count; ++i) {
    if (strcmp(tag_name, possible_tags[i]) == 0) {
      return true;
    }
  }
  return false;
}

RubbishHtmlParser::RubbishHtmlParser(const char *html, int length, const std::string &base_path)
    : base_path_(base_path) {
  parse(html, length);
}

RubbishHtmlParser::~RubbishHtmlParser() {
  for (auto *block : blocks) {
    delete block;
  }
  blocks.clear();

  for (auto *page : pages) {
    delete page;
  }
  pages.clear();
}

bool RubbishHtmlParser::VisitEnter(const tinyxml2::XMLElement &element,
                                   const tinyxml2::XMLAttribute *firstAttribute) {
  (void)firstAttribute;
  const char *tag_name = element.Name();

  if (matches_tag(tag_name, kImageTags, sizeof(kImageTags) / sizeof(kImageTags[0]))) {
    const char *src = element.Attribute("src");
    if (src) {
      BLOCK_STYLE current_style = JUSTIFIED;
      if (current_text_block) {
        current_style = current_text_block->get_style();
        if (current_text_block->is_empty()) {
          blocks.pop_back();
          delete current_text_block;
          current_text_block = nullptr;
        }
      }
      blocks.push_back(new ImageBlock(base_path_ + src));
      start_new_text_block(current_style);
    }
    return true;
  }

  if (matches_tag(tag_name, kSkipTags, sizeof(kSkipTags) / sizeof(kSkipTags[0]))) {
    return false;
  }

  if (matches_tag(tag_name, kHeaderTags, sizeof(kHeaderTags) / sizeof(kHeaderTags[0]))) {
    is_bold = true;
    start_new_text_block(CENTER_ALIGN);
  } else if (matches_tag(tag_name, kBlockTags, sizeof(kBlockTags) / sizeof(kBlockTags[0]))) {
    if (strcmp(tag_name, "br") == 0) {
      start_new_text_block(current_text_block ? current_text_block->get_style() : JUSTIFIED);
    } else {
      start_new_text_block(JUSTIFIED);
    }
  } else if (matches_tag(tag_name, kBoldTags, sizeof(kBoldTags) / sizeof(kBoldTags[0]))) {
    is_bold = true;
  } else if (matches_tag(tag_name, kItalicTags, sizeof(kItalicTags) / sizeof(kItalicTags[0]))) {
    is_italic = true;
  }

  return true;
}

bool RubbishHtmlParser::Visit(const tinyxml2::XMLText &text) {
  add_text(text.Value(), is_bold, is_italic);
  return true;
}

bool RubbishHtmlParser::VisitExit(const tinyxml2::XMLElement &element) {
  const char *tag_name = element.Name();

  if (matches_tag(tag_name, kHeaderTags, sizeof(kHeaderTags) / sizeof(kHeaderTags[0]))) {
    is_bold = false;
  } else if (matches_tag(tag_name, kBoldTags, sizeof(kBoldTags) / sizeof(kBoldTags[0]))) {
    is_bold = false;
  } else if (matches_tag(tag_name, kItalicTags, sizeof(kItalicTags) / sizeof(kItalicTags[0]))) {
    is_italic = false;
  }

  return true;
}

void RubbishHtmlParser::start_new_text_block(BLOCK_STYLE style) {
  if (current_text_block) {
    if (current_text_block->is_empty()) {
      current_text_block->set_style(style);
      return;
    }
    current_text_block->finish();
  }

  current_text_block = new TextBlock(style);
  blocks.push_back(current_text_block);
}

void RubbishHtmlParser::parse(const char *html, int length) {
  start_new_text_block(JUSTIFIED);

  if (!html || length <= 0) {
    return;
  }

  tinyxml2::XMLDocument doc(false, tinyxml2::COLLAPSE_WHITESPACE);
  auto result = doc.Parse(html, length);
  if (result != tinyxml2::XML_SUCCESS) {
    ESP_LOGW(TAG, "XHTML parse failed (%d), fallback to plain text", result);
    add_text(html, false, false);
    return;
  }

  doc.Accept(this);
}

void RubbishHtmlParser::add_text(const char *text, bool bold, bool italic) {
  if (!text || !current_text_block) {
    return;
  }

  std::string parsed = replace_html_entities(std::string(text));
  current_text_block->add_span(parsed.c_str(), bold, italic);
}

void RubbishHtmlParser::layout(Renderer *renderer, ::Epub *epub) {
  if (!renderer || !epub) {
    return;
  }

  for (auto *page : pages) {
    delete page;
  }
  pages.clear();

  const int line_height = renderer->get_line_height();
  const int page_height = renderer->get_page_height();

  for (auto *block : blocks) {
    if (!block) {
      continue;
    }
    block->layout(renderer, epub);
    vTaskDelay(1);
  }

  int y = 0;
  pages.push_back(new Page());

  for (auto *block : blocks) {
    if (!block) {
      continue;
    }

    vTaskDelay(1);

    if (block->getType() == BlockType::TEXT_BLOCK) {
      auto *text_block = static_cast<TextBlock *>(block);
      for (int i = 0; i < static_cast<int>(text_block->line_breaks.size()); ++i) {
        if (y + line_height > page_height) {
          pages.push_back(new Page());
          y = 0;
        }
        pages.back()->elements.push_back(new PageLine(text_block, i, y));
        y += line_height;
      }
      y += line_height / 2;
    } else if (block->getType() == BlockType::IMAGE_BLOCK) {
      auto *image_block = static_cast<ImageBlock *>(block);
      if (y + image_block->height > page_height) {
        pages.push_back(new Page());
        y = 0;
      }
      pages.back()->elements.push_back(new PageImage(image_block, y));
      y += image_block->height;
    }
  }

  if (pages.empty()) {
    pages.push_back(new Page());
  }
}

void RubbishHtmlParser::render_page(int page_index, Renderer *renderer, ::Epub *epub) {
  if (!renderer || !epub) {
    return;
  }

  renderer->clear_screen();

  try {
    pages.at(page_index)->render(renderer, epub);
  } catch (const std::out_of_range &) {
    ESP_LOGW(TAG, "render_page out of range: %d", page_index);
    uint16_t y = static_cast<uint16_t>(renderer->get_page_height() / 2 - 20);
    renderer->draw_rect(1, y, renderer->get_page_width() - 2, 105, 0);
    renderer->draw_text_box("Reached end of chapter", 10, y + 8, renderer->get_page_width() - 20, 80, false, false);
  }
}

std::string RubbishHtmlParser::get_page_plain_text(int page_index, int max_chars) const {
  std::string text;
  if (page_index < 0 || page_index >= static_cast<int>(pages.size())) {
    return text;
  }

  const Page *page = pages[page_index];
  if (!page) {
    return text;
  }

  bool first_line = true;
  for (const auto *element : page->elements) {
    const auto *line = dynamic_cast<const PageLine *>(element);
    if (!line || !line->block) {
      continue;
    }

    std::string line_text;
    line->block->append_line_plain_text(line->line_break_index, line_text);
    if (line_text.empty()) {
      continue;
    }

    if (!first_line) {
      text.push_back('\n');
    }
    first_line = false;
    text.append(line_text);

    if (max_chars > 0 && static_cast<int>(text.size()) >= max_chars) {
      text.resize(static_cast<size_t>(max_chars));
      break;
    }
  }

  return text;
}

}  // namespace fiction_epub_rich
