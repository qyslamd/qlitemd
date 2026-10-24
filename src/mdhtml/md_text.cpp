#include "md_text.h"

#include <algorithm>
#include <cctype>
#include <random>

namespace {

bool IsAsciiSpace(char c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' ||
         c == '\v';
}

std::string ReplaceAll(std::string_view text, std::string_view from,
                       std::string_view to) {
  if (from.empty()) {
    return std::string(text);
  }
  std::string out;
  out.reserve(text.size());
  std::size_t pos = 0;
  while (true) {
    const std::size_t found = text.find(from, pos);
    if (found == std::string_view::npos) {
      out.append(text.substr(pos));
      break;
    }
    out.append(text.substr(pos, found - pos));
    out.append(to);
    pos = found + from.size();
  }
  return out;
}

}  // namespace

std::string_view TrimView(std::string_view text) {
  std::size_t begin = 0;
  while (begin < text.size() && IsAsciiSpace(text[begin])) {
    ++begin;
  }
  std::size_t end = text.size();
  while (end > begin && IsAsciiSpace(text[end - 1])) {
    --end;
  }
  return text.substr(begin, end - begin);
}

std::string Trim(std::string_view text) {
  const std::string_view trimmed = TrimView(text);
  return std::string(trimmed);
}

bool StartsWith(std::string_view text, std::string_view prefix) {
  return text.size() >= prefix.size() &&
         text.substr(0, prefix.size()) == prefix;
}

bool EndsWith(std::string_view text, std::string_view suffix) {
  return text.size() >= suffix.size() &&
         text.substr(text.size() - suffix.size()) == suffix;
}

std::vector<std::string> SplitLines(std::string_view text) {
  std::vector<std::string> lines;
  std::size_t start = 0;
  while (start <= text.size()) {
    const std::size_t newline = text.find('\n', start);
    const std::size_t end =
        newline == std::string_view::npos ? text.size() : newline;
    std::string line(text.substr(start, end - start));
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    lines.push_back(std::move(line));
    if (newline == std::string_view::npos) {
      break;
    }
    start = newline + 1;
  }
  return lines;
}

std::string ToLowerAscii(std::string_view text) {
  std::string out(text);
  for (char& c : out) {
    const unsigned char byte = static_cast<unsigned char>(c);
    if (byte < 0x80) {
      c = static_cast<char>(std::tolower(byte));
    }
  }
  return out;
}

bool DecodeUtf8(std::string_view text, std::size_t offset, uint32_t* code_point,
                std::size_t* consumed) {
  if (code_point == nullptr || consumed == nullptr || offset >= text.size()) {
    return false;
  }
  const unsigned char first = static_cast<unsigned char>(text[offset]);
  if (first < 0x80) {
    *code_point = first;
    *consumed = 1;
    return true;
  }
  int continuation = 0;
  uint32_t value = 0;
  if ((first & 0xE0) == 0xC0) {
    continuation = 1;
    value = first & 0x1F;
  } else if ((first & 0xF0) == 0xE0) {
    continuation = 2;
    value = first & 0x0F;
  } else if ((first & 0xF8) == 0xF0) {
    continuation = 3;
    value = first & 0x07;
  } else {
    *code_point = first;
    *consumed = 1;
    return false;
  }
  if (offset + static_cast<std::size_t>(continuation) >= text.size()) {
    *code_point = first;
    *consumed = 1;
    return false;
  }
  for (int i = 1; i <= continuation; ++i) {
    const unsigned char byte =
        static_cast<unsigned char>(text[offset + static_cast<std::size_t>(i)]);
    if ((byte & 0xC0) != 0x80) {
      *code_point = first;
      *consumed = 1;
      return false;
    }
    value = (value << 6) | (byte & 0x3F);
  }
  *code_point = value;
  *consumed = static_cast<std::size_t>(continuation + 1);
  return true;
}

void AppendUtf8(uint32_t code_point, std::string* out) {
  if (out == nullptr) {
    return;
  }
  if (code_point <= 0x7F) {
    out->push_back(static_cast<char>(code_point));
  } else if (code_point <= 0x7FF) {
    out->push_back(static_cast<char>(0xC0 | (code_point >> 6)));
    out->push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
  } else if (code_point <= 0xFFFF) {
    out->push_back(static_cast<char>(0xE0 | (code_point >> 12)));
    out->push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3F)));
    out->push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
  } else {
    out->push_back(static_cast<char>(0xF0 | (code_point >> 18)));
    out->push_back(static_cast<char>(0x80 | ((code_point >> 12) & 0x3F)));
    out->push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3F)));
    out->push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
  }
}

bool IsUnicodeSpace(uint32_t code_point) {
  if (code_point <= 0x7F) {
    return IsAsciiSpace(static_cast<char>(code_point));
  }
  return code_point == 0x00A0 ||
         (code_point >= 0x2000 && code_point <= 0x200A) ||
         code_point == 0x2028 || code_point == 0x2029 || code_point == 0x202F ||
         code_point == 0x205F || code_point == 0x3000;
}

bool IsUnicodeLetterOrNumber(uint32_t code_point) {
  if (code_point < 0x80) {
    return std::isalnum(static_cast<unsigned char>(code_point)) != 0;
  }
  // Common letter/number ranges. This is intentionally broader than
  // ASCII-only slugs while staying dependency-free.
  return (code_point >= 0x00C0 && code_point <= 0x02AF) ||
         (code_point >= 0x0370 && code_point <= 0x052F) ||
         (code_point >= 0x0531 && code_point <= 0x058F) ||
         (code_point >= 0x0590 && code_point <= 0x05FF) ||
         (code_point >= 0x0600 && code_point <= 0x06FF) ||
         (code_point >= 0x0900 && code_point <= 0x097F) ||
         (code_point >= 0x0E00 && code_point <= 0x0E7F) ||
         (code_point >= 0x1E00 && code_point <= 0x1EFF) ||
         (code_point >= 0x3040 && code_point <= 0x30FF) ||
         (code_point >= 0x3400 && code_point <= 0x4DBF) ||
         (code_point >= 0x4E00 && code_point <= 0x9FFF) ||
         (code_point >= 0xA000 && code_point <= 0xA4CF) ||
         (code_point >= 0xAC00 && code_point <= 0xD7AF) ||
         (code_point >= 0xF900 && code_point <= 0xFAFF) ||
         (code_point >= 0xFF10 && code_point <= 0xFF19) ||
         (code_point >= 0xFF21 && code_point <= 0xFF3A) ||
         (code_point >= 0xFF41 && code_point <= 0xFF5A) ||
         (code_point >= 0x1F300 && code_point <= 0x1FAFF) ||
         (code_point >= 0x20000 && code_point <= 0x3FFFF);
}

std::string Slugify(std::string_view utf8) {
  std::string out;
  out.reserve(utf8.size());
  std::size_t offset = 0;
  while (offset < utf8.size()) {
    uint32_t code_point = 0;
    std::size_t consumed = 0;
    if (!DecodeUtf8(utf8, offset, &code_point, &consumed)) {
      offset += consumed == 0 ? 1 : consumed;
      continue;
    }
    if (IsUnicodeLetterOrNumber(code_point)) {
      if (code_point < 0x80 && code_point >= 'A' && code_point <= 'Z') {
        out.push_back(static_cast<char>(code_point - 'A' + 'a'));
      } else {
        out.append(utf8.substr(offset, consumed));
      }
    } else if (IsUnicodeSpace(code_point)) {
      out.push_back('-');
    } else if (code_point == '-' || code_point == '_') {
      out.push_back(static_cast<char>(code_point));
    }
    offset += consumed;
  }
  return out;
}

std::string EscapeHtml(std::string_view text) {
  std::string out;
  out.reserve(text.size());
  for (char c : text) {
    switch (c) {
      case '&':
        out += "&amp;";
        break;
      case '<':
        out += "&lt;";
        break;
      case '>':
        out += "&gt;";
        break;
      case '"':
        out += "&quot;";
        break;
      default:
        out.push_back(c);
        break;
    }
  }
  return out;
}

std::string UnescapeHtml(std::string_view text) {
  std::string out(text);
  out = ReplaceAll(out, "&quot;", "\"");
  out = ReplaceAll(out, "&lt;", "<");
  out = ReplaceAll(out, "&gt;", ">");
  out = ReplaceAll(out, "&#39;", "'");
  out = ReplaceAll(out, "&apos;", "'");
  // &amp; must be last so "&amp;lt;" is not decoded twice.
  out = ReplaceAll(out, "&amp;", "&");
  return out;
}

std::string RandomHexSalt(std::size_t hex_chars) {
  static constexpr char kHex[] = "0123456789abcdef";
  std::random_device device;
  std::mt19937_64 generator(device());
  std::uniform_int_distribution<int> distribution(0, 15);
  std::string out;
  out.reserve(hex_chars);
  for (std::size_t i = 0; i < hex_chars; ++i) {
    out.push_back(kHex[distribution(generator)]);
  }
  return out;
}
