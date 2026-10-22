#include "TextBlock.h"

#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#ifndef UNIT_TEST
#include <esp_log.h>
#else
#define ESP_LOGI(args...)
#define ESP_LOGE(args...)
#define ESP_LOGD(args...)
#define ESP_LOGW(args...)
#endif

namespace fiction_epub_rich {

static bool is_whitespace(char c) {
  return (c == ' ' || c == '\r' || c == '\n' || c == '\t');
}

static int utf8_char_length(unsigned char c) {
  if (c < 0x80) {
    return 1;
  }
  if ((c & 0xE0) == 0xC0) {
    return 2;
  }
  if ((c & 0xF0) == 0xE0) {
    return 3;
  }
  if ((c & 0xF8) == 0xF0) {
    return 4;
  }
  return 1;
}

static bool is_multibyte_token(const char *token) {
  if (!token || token[0] == '\0') {
    return false;
  }
  return static_cast<unsigned char>(token[0]) >= 0x80;
}

TextBlock::~TextBlock() {
  for (auto span : spans) {
    delete[] span;
  }
}

void TextBlock::add_span(const char *span, bool is_bold, bool is_italic) {
  if (!span) {
    return;
  }

  int length = static_cast<int>(strlen(span));
  if (length <= 0) {
    return;
  }

  int index = 0;
  bool has_space_before = false;
  while (index < length) {
    while (index < length && is_whitespace(span[index])) {
      has_space_before = true;
      index++;
    }

    if (index >= length) {
      break;
    }

    int token_start = index;
    unsigned char lead = static_cast<unsigned char>(span[index]);

    if (lead < 0x80) {
      // Keep ASCII runs together as one token.
      while (index < length) {
        unsigned char c = static_cast<unsigned char>(span[index]);
        if (c >= 0x80 || is_whitespace(static_cast<char>(c))) {
          break;
        }
        index++;
      }
    } else {
      // Split multi-byte UTF-8 text (e.g. CJK) character by character.
      int char_len = utf8_char_length(lead);
      if (index + char_len > length) {
        char_len = 1;
      }
      index += char_len;
    }

    int token_length = index - token_start;
    if (token_length > 0) {
      char *token = new char[token_length + 1];
      memcpy(token, span + token_start, static_cast<size_t>(token_length));
      token[token_length] = '\0';

      spans.push_back(token);
      words.push_back(token);
      word_styles.push_back((is_bold ? BOLD_SPAN : 0) | (is_italic ? ITALIC_SPAN : 0));
      word_has_space_before.push_back(has_space_before ? 1 : 0);
      has_space_before = false;
    }
  }
}

void TextBlock::layout(Renderer *renderer, ::Epub *epub, int max_width) {
  (void)epub;
  if (!renderer) {
    return;
  }

  line_breaks.clear();
  word_widths.clear();
  word_xpos.clear();

  if (words.empty()) {
    return;
  }

  word_widths.reserve(words.size());
  for (size_t i = 0; i < words.size(); ++i) {
    int width = renderer->get_text_width(words[i],
                                         (word_styles[i] & BOLD_SPAN) != 0,
                                         (word_styles[i] & ITALIC_SPAN) != 0);
    if (width < 0) {
      width = 0;
    }
    if (width > INT16_MAX) {
      width = INT16_MAX;
    }
    word_widths.push_back(static_cast<uint16_t>(width));
  }

  int page_width = (max_width > 0) ? max_width : renderer->get_page_width();
  if (page_width <= 0) {
    page_width = 1;
  }
  int natural_space_width = renderer->get_space_width();
  if (natural_space_width < 0) {
    natural_space_width = 0;
  }

  bool has_cjk_tokens = false;
  for (const char *word : words) {
    if (is_multibyte_token(word)) {
      has_cjk_tokens = true;
      break;
    }
  }

  // Add a subtle tracking gap between adjacent CJK glyphs for readability.
  const int cjk_tracking_width = has_cjk_tokens ? 1 : 0;

  // Chinese books usually indent the first line of each paragraph by 2 chars.
  int first_line_indent = 0;
  if (style == JUSTIFIED && has_cjk_tokens) {
    int ch_width = renderer->get_text_width("中", false, false);
    if (ch_width > 0) {
      first_line_indent = ch_width * 2;
      int max_indent = page_width / 3;
      if (first_line_indent > max_indent) {
        first_line_indent = max_indent;
      }
    }
  }

  auto gap_before_word = [&](int word_index, int line_start) -> int {
    if (word_index <= line_start) {
      return 0;
    }
    if (word_index < static_cast<int>(word_has_space_before.size()) && word_has_space_before[word_index]) {
      return natural_space_width;
    }
    if (cjk_tracking_width > 0 &&
        is_multibyte_token(words[word_index - 1]) &&
        is_multibyte_token(words[word_index])) {
      return cjk_tracking_width;
    }
    return 0;
  };

  int n = static_cast<int>(word_widths.size());
  std::vector<int> dp(n, 0);
  std::vector<int> ans(n, 0);

  dp[n - 1] = 0;
  ans[n - 1] = n - 1;

  for (int i = n - 2; i >= 0; --i) {
    int width_limit = page_width - ((i == 0) ? first_line_indent : 0);
    if (width_limit <= 0) {
      width_limit = 1;
    }

    int currlen = 0;
    int best_cost = INT_MAX;
    int best_j = i;

    for (int j = i; j < n; ++j) {
      currlen += gap_before_word(j, i) + static_cast<int>(word_widths[j]);

      if (currlen > width_limit) {
        if (j == i) {
          // Force a too-long token onto its own line.
          int cost = (j == n - 1) ? 0 : dp[j + 1];
          best_cost = cost;
          best_j = j;
        }
        break;
      }

      int cost = 0;
      if (j != n - 1) {
        int spare = width_limit - currlen;
        if (dp[j + 1] >= INT_MAX / 4) {
          continue;
        }
        long long candidate = static_cast<long long>(spare) * static_cast<long long>(spare) +
                              static_cast<long long>(dp[j + 1]);
        cost = (candidate >= INT_MAX) ? INT_MAX : static_cast<int>(candidate);
      }

      if (cost < best_cost) {
        best_cost = cost;
        best_j = j;
      }
    }

    dp[i] = best_cost;
    ans[i] = best_j;
  }

  for (int i = 0; i < n;) {
    int next = ans[i] + 1;
    if (next <= i || next > n) {
      ESP_LOGW("TextBlock", "Invalid line break transition %d -> %d", i, next);
      break;
    }
    line_breaks.push_back(static_cast<uint16_t>(next));
    i = next;

    if (line_breaks.size() > 2000) {
      ESP_LOGE("TextBlock", "Too many line breaks");
      break;
    }
  }

  if (line_breaks.empty()) {
    line_breaks.push_back(static_cast<uint16_t>(n));
  }

  word_xpos.assign(words.size(), 0);

  int start_word = 0;
  for (size_t line_idx = 0; line_idx < line_breaks.size(); ++line_idx) {
    int end_word = line_breaks[line_idx];
    int total_word_width = 0;

    for (int word_index = start_word; word_index < end_word; ++word_index) {
      total_word_width += word_widths[word_index];
    }

    int number_words = end_word - start_word;
    if (number_words <= 0) {
      start_word = end_word;
      continue;
    }

    int line_indent = (line_idx == 0 && start_word == 0) ? first_line_indent : 0;
    int line_width_limit = page_width - line_indent;
    if (line_width_limit <= 0) {
      line_width_limit = 1;
    }

    int base_gaps = 0;
    int adjustable_gaps = 0;
    for (int word_index = start_word + 1; word_index < end_word; ++word_index) {
      int gap = gap_before_word(word_index, start_word);
      base_gaps += gap;
      if (word_index < static_cast<int>(word_has_space_before.size()) && word_has_space_before[word_index]) {
        adjustable_gaps++;
      }
    }

    float spare_space = static_cast<float>(line_width_limit - total_word_width - base_gaps);
    if (spare_space < 0.0f) {
      spare_space = 0.0f;
    }

    bool should_justify =
        (line_idx != line_breaks.size() - 1 && style == JUSTIFIED && adjustable_gaps > 0);
    float extra_space_per_gap = should_justify ? (spare_space / static_cast<float>(adjustable_gaps)) : 0.0f;

    float xpos = static_cast<float>(line_indent);
    if (!should_justify && style == RIGHT_ALIGN) {
      xpos += spare_space;
    } else if (style == CENTER_ALIGN) {
      xpos += spare_space / 2.0f;
    }

    if (xpos < 0.0f) {
      xpos = 0.0f;
    }

    for (int word_index = start_word; word_index < end_word; ++word_index) {
      if (word_index > start_word) {
        int gap = gap_before_word(word_index, start_word);
        xpos += static_cast<float>(gap);

        bool has_adjustable_gap =
            (word_index < static_cast<int>(word_has_space_before.size()) && word_has_space_before[word_index]);
        if (has_adjustable_gap && should_justify) {
          xpos += extra_space_per_gap;
        }
      }

      word_xpos[word_index] = static_cast<uint16_t>(xpos < 0.0f ? 0.0f : xpos);
      xpos += static_cast<float>(word_widths[word_index]);
    }

    start_word = end_word;
  }

  spans.shrink_to_fit();
  words.shrink_to_fit();
  word_widths.shrink_to_fit();
  word_xpos.shrink_to_fit();
  word_styles.shrink_to_fit();
  word_has_space_before.shrink_to_fit();
}

void TextBlock::render(Renderer *renderer, int line_break_index, int x_pos, int y_pos) {
  if (!renderer || line_break_index < 0 || line_break_index >= static_cast<int>(line_breaks.size())) {
    return;
  }

  int start = (line_break_index == 0) ? 0 : line_breaks[line_break_index - 1];
  int end = line_breaks[line_break_index];

  for (int i = start; i < end; ++i) {
    uint8_t style_flags = word_styles[i];
    renderer->draw_text(x_pos + word_xpos[i],
                        y_pos,
                        words[i],
                        (style_flags & BOLD_SPAN) != 0,
                        (style_flags & ITALIC_SPAN) != 0);
  }
}

void TextBlock::dump() {
  for (size_t i = 0; i < words.size(); ++i) {
    printf("##%d#%s## ", word_widths[i], words[i]);
  }
}

void TextBlock::append_line_plain_text(int line_break_index, std::string &out) const {
  if (line_break_index < 0 || line_break_index >= static_cast<int>(line_breaks.size())) {
    return;
  }

  int start = (line_break_index == 0) ? 0 : line_breaks[line_break_index - 1];
  int end = line_breaks[line_break_index];
  if (start < 0 || end < start || end > static_cast<int>(words.size())) {
    return;
  }

  for (int i = start; i < end; ++i) {
    if (i > start && i < static_cast<int>(word_has_space_before.size()) && word_has_space_before[i]) {
      out.push_back(' ');
    }
    if (words[i]) {
      out.append(words[i]);
    }
  }
}

}  // namespace fiction_epub_rich
