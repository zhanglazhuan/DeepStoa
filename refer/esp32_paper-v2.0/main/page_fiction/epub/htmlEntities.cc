#include "htmlEntities.h"

#include <cstdlib>
#include <cstring>
#include <string>

namespace {

constexpr int MAX_ENTITY_LENGTH = 12;

struct EntityMapping {
  const char *name;
  const char *value;
};

// Keep this table static/constexpr-like (const char* only) so startup has no heap activity.
static const EntityMapping kEntityMap[] = {
    {"&quot;", "\""},   {"&apos;", "'"},   {"&amp;", "&"},
    {"&lt;", "<"},       {"&gt;", ">"},      {"&nbsp;", " "},
    {"&ensp;", " "},     {"&emsp;", " "},    {"&thinsp;", " "},
    {"&mdash;", "—"},   {"&ndash;", "–"},   {"&hellip;", "…"},
    {"&lsquo;", "‘"},   {"&rsquo;", "’"},   {"&ldquo;", "“"},
    {"&rdquo;", "”"},   {"&bull;", "•"},    {"&trade;", "™"},
    {"&copy;", "©"},    {"&reg;", "®"},     {"&euro;", "€"},
    {"&deg;", "°"},     {"&plusmn;", "±"}, {"&times;", "×"},
    {"&divide;", "÷"},  {"&laquo;", "«"},   {"&raquo;", "»"},
    {"&lsaquo;", "‹"},  {"&rsaquo;", "›"},  {"&iexcl;", "¡"},
    {"&iquest;", "¿"},  {"&frac14;", "¼"},  {"&frac12;", "½"},
    {"&frac34;", "¾"},  {"&yen;", "¥"},     {"&pound;", "£"},
    {"&cent;", "¢"},    {"&sect;", "§"},    {"&para;", "¶"},
    {"&alpha;", "α"},   {"&beta;", "β"},    {"&gamma;", "γ"},
    {"&delta;", "δ"},   {"&epsilon;", "ε"}, {"&theta;", "θ"},
    {"&lambda;", "λ"},  {"&mu;", "μ"},      {"&pi;", "π"},
    {"&sigma;", "σ"},   {"&phi;", "φ"},     {"&omega;", "ω"},
    {"&larr;", "←"},    {"&uarr;", "↑"},    {"&rarr;", "→"},
    {"&darr;", "↓"},    {"&harr;", "↔"},    {"&ne;", "≠"},
    {"&le;", "≤"},      {"&ge;", "≥"},      {"&infin;", "∞"},
    {"&radic;", "√"},   {"&sum;", "∑"},     {"&prod;", "∏"},
    {"&int;", "∫"},     {"&minus;", "−"},   {"&permil;", "‰"},
    {"&prime;", "′"},   {"&Prime;", "″"},   {"&oline;", "‾"},
    {"&frasl;", "⁄"},   {"&loz;", "◊"},     {"&spades;", "♠"},
    {"&clubs;", "♣"},   {"&hearts;", "♥"},  {"&diams;", "♦"},
};

static bool append_utf8(uint32_t code, std::string &out) {
  if (code <= 0x7F) {
    out.push_back(static_cast<char>(code));
    return true;
  }
  if (code <= 0x7FF) {
    out.push_back(static_cast<char>(0xC0 | (code >> 6)));
    out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
    return true;
  }
  if (code >= 0xD800 && code <= 0xDFFF) {
    return false;
  }
  if (code <= 0xFFFF) {
    out.push_back(static_cast<char>(0xE0 | (code >> 12)));
    out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
    return true;
  }
  if (code <= 0x10FFFF) {
    out.push_back(static_cast<char>(0xF0 | (code >> 18)));
    out.push_back(static_cast<char>(0x80 | ((code >> 12) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
    return true;
  }
  return false;
}

static bool process_numeric_entity(const std::string &entity, std::string &out) {
  if (entity.size() < 4 || entity[0] != '&' || entity[1] != '#' || entity.back() != ';') {
    return false;
  }

  const bool is_hex = (entity.size() > 4) && (entity[2] == 'x' || entity[2] == 'X');
  const char *begin = entity.c_str() + (is_hex ? 3 : 2);
  const char *end_limit = entity.c_str() + entity.size() - 1;

  char *end_ptr = nullptr;
  long value = strtol(begin, &end_ptr, is_hex ? 16 : 10);
  if (!end_ptr || end_ptr != end_limit || value <= 0) {
    return false;
  }

  if (value == 0xA0) {
    out.push_back(' ');
    return true;
  }

  return append_utf8(static_cast<uint32_t>(value), out);
}

static bool process_named_entity(const std::string &entity, std::string &out) {
  for (const auto &item : kEntityMap) {
    if (entity == item.name) {
      out += item.value;
      return true;
    }
  }
  return false;
}

}  // namespace

std::string replace_html_entities(const std::string &text) {
  std::string out;
  out.reserve(text.size());

  for (size_t i = 0; i < text.size(); ++i) {
    if (text[i] != '&') {
      out.push_back(text[i]);
      continue;
    }

    size_t j = i + 1;
    while (j < text.size() && text[j] != ';' && static_cast<int>(j - i) <= MAX_ENTITY_LENGTH) {
      ++j;
    }

    if (j >= text.size() || text[j] != ';' || j <= i + 1) {
      out.push_back(text[i]);
      continue;
    }

    std::string entity = text.substr(i, j - i + 1);
    bool decoded = false;

    if (entity.size() > 3 && entity[1] == '#') {
      decoded = process_numeric_entity(entity, out);
    } else {
      decoded = process_named_entity(entity, out);
    }

    if (decoded) {
      i = j;
    } else {
      out.push_back(text[i]);
    }
  }

  return out;
}

