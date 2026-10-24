#include "md_font_metrics.h"

#include <cctype>
#include <cstdint>
#include <string>
#include <string_view>

#include "md_text.h"

namespace {

std::string_view TrimLocal(std::string_view text) {
  std::size_t begin = 0;
  std::size_t end = text.size();
  while (begin < end && (text[begin] == ' ' || text[begin] == '\t' ||
                         text[begin] == '\n' || text[begin] == '\r')) {
    ++begin;
  }
  while (end > begin && (text[end - 1] == ' ' || text[end - 1] == '\t' ||
                         text[end - 1] == '\n' || text[end - 1] == '\r')) {
    --end;
  }
  return text.substr(begin, end - begin);
}

std::string_view Unquote(std::string_view family) {
  if (family.size() >= 2 &&
      ((family.front() == '"' && family.back() == '"') ||
       (family.front() == '\'' && family.back() == '\''))) {
    return family.substr(1, family.size() - 2);
  }
  return family;
}

// Advance in em units. The table is deliberately coarse: it only has to
// keep generated SVG text inside its boxes, not match a specific font.
double AdvanceEm(uint32_t code_point) {
  if (code_point == 0) {
    return 0.0;
  }
  if (code_point == ' ') {
    return 0.28;
  }
  if (code_point < 0x80) {
    const char c = static_cast<char>(code_point);
    switch (c) {
      case 'i':
      case 'l':
      case 'j':
      case '.':
      case ',':
      case '\'':
      case '!':
      case '|':
      case ':':
      case ';':
        return 0.28;
      case 'm':
      case 'w':
      case 'M':
      case 'W':
        return 0.85;
      case 'f':
      case 't':
      case 'r':
        return 0.38;
      default:
        break;
    }
    if (std::isupper(static_cast<unsigned char>(c)) != 0) {
      return 0.62;
    }
    if (std::isdigit(static_cast<unsigned char>(c)) != 0) {
      return 0.56;
    }
    return 0.52;
  }
  if (code_point >= 0x0300 && code_point <= 0x036F) {
    return 0.0;  // combining marks attach to the previous glyph
  }
  if (code_point >= 0x2E80) {
    return 1.0;  // CJK, fullwidth forms, emoji
  }
  return 0.6;
}

}  // namespace

std::string MdDefaultFontMetrics::ResolveFamily(
    std::string_view css_list) const {
  std::string_view list = TrimLocal(css_list);
  std::size_t start = 0;
  while (start <= list.size()) {
    const std::size_t comma = list.find(',', start);
    const std::size_t end =
        comma == std::string_view::npos ? list.size() : comma;
    std::string_view family = TrimLocal(list.substr(start, end - start));
    while (!family.empty() && family.back() == ';') {
      family = TrimLocal(family.substr(0, family.size() - 1));
    }
    family = Unquote(family);
    if (!family.empty()) {
      return std::string(family);
    }
    if (comma == std::string_view::npos) {
      break;
    }
    start = comma + 1;
  }
  return "sans-serif";
}

MdTextMetrics MdDefaultFontMetrics::Measure(std::string_view text,
                                            const MdFontSpec& font) const {
  MdTextMetrics metrics;
  if (text.empty() || font.size_px <= 0.0) {
    return metrics;
  }
  double advance_em = 0.0;
  std::size_t offset = 0;
  while (offset < text.size()) {
    uint32_t code_point = 0;
    std::size_t consumed = 1;
    if (!DecodeUtf8(text, offset, &code_point, &consumed)) {
      // Invalid bytes still occupy space; never loop without progress.
      code_point = 0;
      consumed = 1;
      advance_em += 0.5;
    } else {
      advance_em += AdvanceEm(code_point);
    }
    offset += consumed;
  }
  metrics.width = advance_em * font.size_px;
  metrics.ascent = font.size_px * 0.8;
  metrics.descent = font.size_px * 0.2;
  metrics.line_count = 1;
  return metrics;
}
