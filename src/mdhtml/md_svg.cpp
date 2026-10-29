#include "md_svg.h"

#include <cmath>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

namespace {

bool IsSpace(char c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' ||
         c == '\v';
}

// Parses one 0..255 channel from "r, g, b[, a]" and advances past it.
bool ParseRgbaChannel(const std::string& body, std::size_t* pos, int* value) {
  while (*pos < body.size() && (IsSpace(body[*pos]) || body[*pos] == ',')) {
    ++*pos;
  }
  const char* begin = body.c_str() + *pos;
  char* end = nullptr;
  const long parsed = std::strtol(begin, &end, 10);
  if (end == begin || parsed < 0 || parsed > 255) {
    return false;
  }
  *pos += static_cast<std::size_t>(end - begin);
  *value = static_cast<int>(parsed);
  return true;
}

std::string_view TrimViewLocal(std::string_view text) {
  std::size_t begin = 0;
  std::size_t end = text.size();
  while (begin < end && IsSpace(text[begin])) {
    ++begin;
  }
  while (end > begin && IsSpace(text[end - 1])) {
    --end;
  }
  return text.substr(begin, end - begin);
}

// Finds a quoted attribute value inside a tag. The name must start a token
// (after whitespace or '<') and be followed by '=' after optional spaces, so
// "width" does not match "stroke-width".
std::string_view FindAttribute(std::string_view tag, std::string_view name) {
  std::size_t pos = 0;
  while ((pos = tag.find(name, pos)) != std::string_view::npos) {
    const bool boundary =
        pos == 0 || IsSpace(tag[pos - 1]) || tag[pos - 1] == '<';
    std::size_t cursor = pos + name.size();
    if (boundary) {
      while (cursor < tag.size() && IsSpace(tag[cursor])) {
        ++cursor;
      }
      if (cursor < tag.size() && tag[cursor] == '=') {
        ++cursor;
        while (cursor < tag.size() && IsSpace(tag[cursor])) {
          ++cursor;
        }
        if (cursor < tag.size() &&
            (tag[cursor] == '"' || tag[cursor] == '\'')) {
          const char quote = tag[cursor];
          const std::size_t end = tag.find(quote, cursor + 1);
          if (end != std::string_view::npos) {
            return tag.substr(cursor + 1, end - cursor - 1);
          }
        }
      }
    }
    pos += name.size();
  }
  return {};
}

std::vector<double> ParseNumberList(std::string_view text) {
  std::string buffer(text);
  for (char& c : buffer) {
    if (c == ',') {
      c = ' ';
    }
  }
  std::vector<double> numbers;
  const char* cursor = buffer.c_str();
  while (*cursor != '\0') {
    char* end = nullptr;
    const double value = std::strtod(cursor, &end);
    if (end == cursor) {
      ++cursor;
      continue;
    }
    numbers.push_back(value);
    cursor = end;
  }
  return numbers;
}

bool ParsePixelLength(std::string_view text, double* out) {
  std::string_view value = TrimViewLocal(text);
  if (value.size() >= 2 && value.substr(value.size() - 2) == "px") {
    value = TrimViewLocal(value.substr(0, value.size() - 2));
  }
  if (value.empty() || value.find('%') != std::string_view::npos) {
    return false;
  }
  // Any remaining alphabetic suffix (em/ex/pt/...) is not an intrinsic
  // pixel length.
  for (char c : value) {
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) {
      return false;
    }
  }
  const std::vector<double> numbers = ParseNumberList(value);
  if (numbers.size() != 1 || !std::isfinite(numbers[0]) || numbers[0] <= 0) {
    return false;
  }
  *out = numbers[0];
  return true;
}

std::string_view RootTag(std::string_view root) {
  const std::size_t end = root.find('>');
  return end == std::string_view::npos ? root : root.substr(0, end + 1);
}

bool HasFillAttribute(std::string_view tag) {
  // Presence matters, not the value: duplicate fill attributes are illegal
  // XML, even an empty one counts as the payload's own colour choice.
  std::size_t pos = 0;
  while ((pos = tag.find("fill", pos)) != std::string_view::npos) {
    const bool boundary =
        pos == 0 || IsSpace(tag[pos - 1]) || tag[pos - 1] == '<';
    std::size_t cursor = pos + 4;
    if (boundary) {
      while (cursor < tag.size() && IsSpace(tag[cursor])) {
        ++cursor;
      }
      if (cursor < tag.size() && tag[cursor] == '=') {
        return true;
      }
    }
    pos += 4;
  }
  return false;
}

}  // namespace

std::string MdExtractSvgRoot(std::string_view svg) {
  if (svg.size() > kMdMaxSvgBytes) {
    return {};
  }
  const std::size_t start = svg.find("<svg");
  if (start == std::string_view::npos) {
    return {};
  }
  const std::size_t end = svg.rfind("</svg>");
  if (end == std::string_view::npos || end < start) {
    return {};
  }
  return std::string(svg.substr(start, end - start + 6));
}

bool MdSvgIntrinsicSize(std::string_view svg, int* width, int* height) {
  if (width == nullptr || height == nullptr) {
    return false;
  }
  const std::string root = MdExtractSvgRoot(svg);
  if (root.empty()) {
    return false;
  }
  const std::string_view tag = RootTag(root);

  const std::vector<double> view_box =
      ParseNumberList(FindAttribute(tag, "viewBox"));
  if (view_box.size() == 4 && view_box[2] >= 1.0 && view_box[3] >= 1.0) {
    *width = static_cast<int>(std::lround(view_box[2]));
    *height = static_cast<int>(std::lround(view_box[3]));
    return *width > 0 && *height > 0;
  }

  double parsed_width = 0.0;
  double parsed_height = 0.0;
  if (!ParsePixelLength(FindAttribute(tag, "width"), &parsed_width) ||
      !ParsePixelLength(FindAttribute(tag, "height"), &parsed_height)) {
    return false;
  }
  *width = static_cast<int>(std::lround(parsed_width));
  *height = static_cast<int>(std::lround(parsed_height));
  return *width > 0 && *height > 0;
}

std::string MdColorHex(MdColor color) {
  constexpr char kHex[] = "0123456789abcdef";
  std::string out = "#";
  for (uint8_t channel : {color.r, color.g, color.b}) {
    out.push_back(kHex[channel >> 4]);
    out.push_back(kHex[channel & 0x0F]);
  }
  return out;
}

std::string MdSvgMakePaintPortable(std::string_view svg) {
  std::string result(svg);
  std::size_t pos = 0;
  while ((pos = result.find("rgba(", pos)) != std::string::npos) {
    const std::size_t end = result.find(')', pos + 5);
    if (end == std::string::npos) {
      break;
    }
    const std::string body = result.substr(pos + 5, end - pos - 5);
    std::size_t cursor = 0;
    int r = 0;
    int g = 0;
    int b = 0;
    if (ParseRgbaChannel(body, &cursor, &r) &&
        ParseRgbaChannel(body, &cursor, &g) &&
        ParseRgbaChannel(body, &cursor, &b)) {
      const std::string hex =
          MdColorHex(MdColor{static_cast<uint8_t>(r), static_cast<uint8_t>(g),
                             static_cast<uint8_t>(b), 0xff});
      result.replace(pos, end - pos + 1, hex);
      pos += hex.size();
    } else {
      pos += 5;
    }
  }
  return result;
}

std::string MdApplySvgColor(std::string_view svg, MdColor color) {
  std::string result(svg);
  const std::string hex = MdColorHex(color);
  constexpr std::string_view kCurrentColor = "currentColor";
  if (result.find(kCurrentColor) != std::string::npos) {
    std::size_t pos = 0;
    while ((pos = result.find(kCurrentColor, pos)) != std::string::npos) {
      result.replace(pos, kCurrentColor.size(), hex);
      pos += hex.size();
    }
    return result;
  }

  // MathJax writes fill/stroke="currentColor" on the root group; payloads
  // without it get a fill on the first group so dark themes stay readable.
  std::size_t group = result.find("<g");
  while (group != std::string::npos) {
    const char next = group + 2 < result.size() ? result[group + 2] : '\0';
    if (next == ' ' || next == '>' || next == '\t' || next == '\n') {
      break;
    }
    group = result.find("<g", group + 2);
  }
  if (group == std::string::npos) {
    return result;
  }
  const std::size_t tag_end = result.find('>', group);
  if (tag_end == std::string::npos) {
    return result;
  }
  const std::string_view tag(result.data() + group, tag_end - group);
  const bool self_closing = result[tag_end - 1] == '/';
  if (self_closing || HasFillAttribute(tag)) {
    return result;
  }
  result.insert(tag_end, " fill=\"" + hex + "\"");
  return result;
}
