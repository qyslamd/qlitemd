#include "md_callout.h"

#include <cstddef>
#include <string>
#include <string_view>

namespace {

constexpr std::string_view kBlockquoteOpen = "<blockquote>";
constexpr std::string_view kBlockquoteClose = "</blockquote>";
constexpr std::string_view kParagraphOpen = "<p>";
constexpr std::string_view kParagraphClose = "</p>";

// The icon is emitted as a span instead of CSS ::before: not every litehtml
// host implements generated content, and MPE itself uses explicit icon
// elements. These are text glyphs, not emoji images.
struct CalloutType {
  std::string_view name;
  std::string_view title;
  std::string_view icon;
};

constexpr CalloutType kCalloutTypes[] = {
    {"note", "Note", "✎"},         {"info", "Info", "ℹ"},
    {"tip", "Tip", "✦"},           {"important", "Important", "✦"},
    {"warning", "Warning", "△"},   {"caution", "Caution", "△"},
    {"danger", "Danger", "×"},     {"success", "Success", "✓"},
    {"question", "Question", "?"},
};

const CalloutType* FindCalloutType(std::string_view name) {
  for (const CalloutType& type : kCalloutTypes) {
    if (type.name == name) {
      return &type;
    }
  }
  return nullptr;
}

bool IsAsciiSpace(char c) {
  return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

std::string_view TrimAscii(std::string_view text) {
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

std::string ToLowerAscii(std::string_view text) {
  std::string out;
  out.reserve(text.size());
  for (char c : text) {
    if (c >= 'A' && c <= 'Z') {
      out.push_back(static_cast<char>(c - 'A' + 'a'));
    } else {
      out.push_back(c);
    }
  }
  return out;
}

std::size_t FindBlockquoteClose(std::string_view html, std::size_t from) {
  std::size_t depth = 1;
  std::size_t pos = from;
  while (pos < html.size()) {
    const std::size_t open = html.find(kBlockquoteOpen, pos);
    const std::size_t close = html.find(kBlockquoteClose, pos);
    if (close == std::string_view::npos) {
      return std::string_view::npos;
    }
    if (open != std::string_view::npos && open < close) {
      ++depth;
      pos = open + kBlockquoteOpen.size();
      continue;
    }
    --depth;
    if (depth == 0) {
      return close;
    }
    pos = close + kBlockquoteClose.size();
  }
  return std::string_view::npos;
}

// Parses "[!type] title" from the first line. Returns false when the line is
// not a supported callout marker.
bool ParseMarker(std::string_view line, std::string* type, std::string* title) {
  line = TrimAscii(line);
  if (line.size() < 4 || line.substr(0, 2) != "[!") {
    return false;
  }
  const std::size_t close = line.find(']');
  if (close == std::string_view::npos || close < 3) {
    return false;
  }
  const std::size_t after = close + 1;
  if (after < line.size() && line[after] != ' ' && line[after] != '\t') {
    // MPE requires whitespace between the marker and an inline title.
    return false;
  }
  const std::string lowered = ToLowerAscii(line.substr(2, close - 2));
  const CalloutType* found = FindCalloutType(lowered);
  if (found == nullptr) {
    return false;
  }
  *type = lowered;
  *title = std::string(TrimAscii(line.substr(after)));
  return true;
}

bool TryRewriteCallout(std::string_view inner, std::string* out) {
  std::size_t start = 0;
  while (start < inner.size() && IsAsciiSpace(inner[start])) {
    ++start;
  }
  if (inner.substr(start, kParagraphOpen.size()) != kParagraphOpen) {
    return false;
  }
  const std::size_t p_start = start + kParagraphOpen.size();
  const std::size_t p_end = inner.find(kParagraphClose, p_start);
  if (p_end == std::string_view::npos) {
    return false;
  }

  const std::string_view paragraph = inner.substr(p_start, p_end - p_start);
  const std::size_t newline = paragraph.find('\n');
  const std::string_view first_line = newline == std::string_view::npos
                                          ? paragraph
                                          : paragraph.substr(0, newline);
  const std::string_view body = newline == std::string_view::npos
                                    ? std::string_view()
                                    : paragraph.substr(newline + 1);

  std::string type;
  std::string inline_title;
  if (!ParseMarker(first_line, &type, &inline_title)) {
    return false;
  }
  const CalloutType* info = FindCalloutType(type);
  if (info == nullptr) {
    return false;
  }
  const std::string title =
      inline_title.empty() ? std::string(info->title) : inline_title;
  const std::string_view trimmed_body = TrimAscii(body);
  const std::string_view rest = inner.substr(p_end + kParagraphClose.size());

  out->clear();
  out->reserve(inner.size() + 64);
  out->append("<div class=\"callout callout-");
  out->append(type);
  out->append(
      "\">\n<div class=\"callout-title\"><span class=\"callout-icon\">");
  out->append(info->icon);
  out->append("</span>");
  out->append(title);
  out->append("</div>\n");
  if (!trimmed_body.empty()) {
    out->append("<p>");
    out->append(trimmed_body);
    out->append("</p>\n");
  }
  out->append(rest);
  out->append("</div>\n");
  return true;
}

}  // namespace

std::string ApplyMpeCallouts(std::string_view html) {
  std::string out;
  out.reserve(html.size());
  std::size_t pos = 0;
  while (pos < html.size()) {
    const std::size_t open = html.find(kBlockquoteOpen, pos);
    if (open == std::string_view::npos) {
      out.append(html.substr(pos));
      break;
    }
    out.append(html.substr(pos, open - pos));
    const std::size_t inner_start = open + kBlockquoteOpen.size();
    const std::size_t close = FindBlockquoteClose(html, inner_start);
    if (close == std::string_view::npos) {
      out.append(html.substr(open));
      break;
    }
    const std::string_view inner =
        html.substr(inner_start, close - inner_start);
    std::string processed = ApplyMpeCallouts(inner);
    std::string rewritten;
    if (TryRewriteCallout(processed, &rewritten)) {
      out.append(rewritten);
    } else {
      out.append(kBlockquoteOpen);
      out.append(processed);
      out.append(kBlockquoteClose);
    }
    pos = close + kBlockquoteClose.size();
  }
  return out;
}
