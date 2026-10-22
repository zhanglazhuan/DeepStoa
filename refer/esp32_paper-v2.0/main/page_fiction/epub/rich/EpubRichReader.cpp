#include "EpubRichReader.h"

#ifndef UNIT_TEST
#include <esp_log.h>
#else
#define ESP_LOGI(args...)
#define ESP_LOGW(args...)
#define ESP_LOGE(args...)
#endif

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <cstdio>

#include "../Epub.h"
#include "Renderer.h"
#include "RubbishHtmlParser.h"

namespace fiction_epub_rich {

static const char *TAG = "EPUB_RICH";

EpubRichReader::EpubRichReader(RichReadState &state, Renderer *renderer)
    : state_(state), renderer_(renderer) {}

EpubRichReader::~EpubRichReader() {
  clear_parser();
  delete epub_;
  epub_ = nullptr;
}

void EpubRichReader::clear_parser() {
  delete parser_;
  parser_ = nullptr;
}

bool EpubRichReader::load() {
  if (state_.path.empty()) {
    ESP_LOGE(TAG, "EPUB path is empty");
    return false;
  }

  bool need_reload = false;
  if (!epub_) {
    need_reload = true;
  } else if (epub_->get_path() != state_.path) {
    need_reload = true;
  }

  if (!need_reload) {
    return true;
  }

  clear_parser();
  delete epub_;
  epub_ = new Epub(state_.path);

  if (!epub_->load()) {
    ESP_LOGE(TAG, "Failed to load epub: %s", state_.path.c_str());
    delete epub_;
    epub_ = nullptr;
    return false;
  }

  int spine_count = epub_->get_spine_items_count();
  if (spine_count <= 0) {
    ESP_LOGE(TAG, "EPUB has empty spine: %s", state_.path.c_str());
    return false;
  }

  if (state_.current_section >= static_cast<uint16_t>(spine_count)) {
    state_.current_section = static_cast<uint16_t>(spine_count - 1);
  }

  state_.pages_in_current_section = 0;
  if (state_.current_page > 0x7FFF) {
    state_.current_page = 0;
  }

  return true;
}

bool EpubRichReader::parse_and_layout_current_section() {
  if (!load()) {
    return false;
  }
  if (!renderer_) {
    ESP_LOGE(TAG, "Renderer is null");
    return false;
  }

  int spine_count = epub_->get_spine_items_count();
  if (spine_count <= 0) {
    return false;
  }

  if (state_.current_section >= static_cast<uint16_t>(spine_count)) {
    state_.current_section = static_cast<uint16_t>(spine_count - 1);
  }

  if (parser_) {
    int count = parser_->get_page_count();
    if (count <= 0) {
      count = 1;
    }
    state_.pages_in_current_section = static_cast<uint16_t>(count);
    if (state_.current_page >= state_.pages_in_current_section) {
      state_.current_page = state_.pages_in_current_section - 1;
    }
    return true;
  }

  std::string spine_item = epub_->get_spine_item(state_.current_section);
  if (spine_item.empty()) {
    ESP_LOGE(TAG, "Spine item is empty for section %u", static_cast<unsigned>(state_.current_section));
    return false;
  }

  size_t html_size = 0;
  uint8_t *html_data = epub_->get_item_contents(spine_item, &html_size);
  if (!html_data || html_size == 0) {
    ESP_LOGE(TAG, "Failed to load section html: %s", spine_item.c_str());
    if (html_data) {
      free(html_data);
    }
    return false;
  }

  std::string base_path;
  size_t slash_pos = spine_item.find_last_of('/');
  if (slash_pos != std::string::npos) {
    base_path = spine_item.substr(0, slash_pos + 1);
  }

  parser_ = new RubbishHtmlParser(reinterpret_cast<char *>(html_data), static_cast<int>(html_size), base_path);
  free(html_data);

  parser_->layout(renderer_, epub_);

  int page_count = parser_->get_page_count();
  if (page_count <= 0) {
    page_count = 1;
  }

  state_.pages_in_current_section = static_cast<uint16_t>(page_count);
  if (state_.current_page >= state_.pages_in_current_section) {
    state_.current_page = state_.pages_in_current_section - 1;
  }

  return true;
}

bool EpubRichReader::render() {
  if (!parse_and_layout_current_section()) {
    return false;
  }

  parser_->render_page(state_.current_page, renderer_, epub_);
  return true;
}

bool EpubRichReader::next() {
  if (!parse_and_layout_current_section()) {
    return false;
  }

  if (state_.current_page + 1 < state_.pages_in_current_section) {
    state_.current_page++;
    return true;
  }

  int spine_count = epub_->get_spine_items_count();
  if (state_.current_section + 1 >= spine_count) {
    return false;
  }

  state_.current_section++;
  state_.current_page = 0;
  clear_parser();
  return parse_and_layout_current_section();
}

bool EpubRichReader::prev() {
  if (!parse_and_layout_current_section()) {
    return false;
  }

  if (state_.current_page > 0) {
    state_.current_page--;
    return true;
  }

  if (state_.current_section == 0) {
    return false;
  }

  state_.current_section--;
  state_.current_page = 0;
  clear_parser();

  if (!parse_and_layout_current_section()) {
    return false;
  }

  if (state_.pages_in_current_section > 0) {
    state_.current_page = state_.pages_in_current_section - 1;
  }

  return true;
}

bool EpubRichReader::jump_to(uint16_t section, uint16_t page) {
  if (!load()) {
    return false;
  }

  int spine_count = epub_->get_spine_items_count();
  if (spine_count <= 0) {
    return false;
  }

  if (section >= static_cast<uint16_t>(spine_count)) {
    section = static_cast<uint16_t>(spine_count - 1);
  }

  bool section_changed = (section != state_.current_section);
  state_.current_section = section;
  state_.current_page = page;

  if (section_changed) {
    clear_parser();
  }

  return parse_and_layout_current_section();
}

void EpubRichReader::invalidate_layout() {
  clear_parser();
}

int EpubRichReader::get_spine_count() const {
  return epub_ ? epub_->get_spine_items_count() : 0;
}

const std::string &EpubRichReader::get_title() const {
  static const std::string kEmpty;
  if (!epub_) {
    return kEmpty;
  }
  return epub_->get_title();
}

int EpubRichReader::get_toc_count() const {
  return epub_ ? epub_->get_toc_items_count() : 0;
}

std::string EpubRichReader::get_toc_title(int toc_index) const {
  if (!epub_) {
    return std::string();
  }
  if (toc_index < 0 || toc_index >= epub_->get_toc_items_count()) {
    return std::string();
  }
  const EpubTocEntry &entry = epub_->get_toc_item(toc_index);
  if (!entry.title.empty()) {
    return entry.title;
  }
  int section = epub_->get_spine_index_for_toc_index(toc_index);
  if (section >= 0) {
    char fallback[32];
    snprintf(fallback, sizeof(fallback), "章节 %d", section + 1);
    return std::string(fallback);
  }
  return std::string("未命名目录");
}

int EpubRichReader::get_section_for_toc(int toc_index) const {
  if (!epub_) {
    return -1;
  }
  return epub_->get_spine_index_for_toc_index(toc_index);
}

std::string EpubRichReader::get_current_page_text(int max_chars) const {
  if (!parser_) {
    return std::string();
  }
  return parser_->get_page_plain_text(static_cast<int>(state_.current_page), max_chars);
}

}  // namespace fiction_epub_rich
