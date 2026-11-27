#include "md_parser.h"

#include <cctype>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "md_block_model.h"
#include "md_callout.h"
#include "md_text.h"

extern "C" {
#include "md4c-html.h"
}

namespace {

constexpr std::string_view kMathPrefix = "@@mj-";

std::string MathPlaceholder(std::string_view salt, std::size_t index) {
  std::string out(kMathPrefix);
  out.append(salt);
  out.push_back('-');
  out.append(std::to_string(index));
  out.append("@@");
  return out;
}

bool IsAsciiSpace(char c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' ||
         c == '\v';
}

bool IsEscapedAt(std::string_view line, std::size_t index) {
  int backslashes = 0;
  for (std::size_t i = index; i > 0 && line[i - 1] == '\\'; --i) {
    ++backslashes;
  }
  return backslashes % 2 == 1;
}

std::string ExtractMathInLine(std::string_view line,
                              std::vector<MdMathSegment>* segments,
                              std::string_view salt) {
  std::string out;
  out.reserve(line.size());
  std::size_t i = 0;
  while (i < line.size()) {
    const char current = line[i];
    if (current == '`') {
      std::size_t run = 1;
      while (i + run < line.size() && line[i + run] == '`') {
        ++run;
      }
      const std::string closing(run, '`');
      const std::size_t close = line.find(closing, i + run);
      if (close == std::string_view::npos) {
        out.append(line.substr(i));
        break;
      }
      out.append(line.substr(i, close + run - i));
      i = close + run;
      continue;
    }
    if (current != '$') {
      out.push_back(current);
      ++i;
      continue;
    }
    if (IsEscapedAt(line, i)) {
      out.push_back(current);
      ++i;
      continue;
    }
    if (i + 1 < line.size() && line[i + 1] == '$') {
      std::size_t close = std::string_view::npos;
      for (std::size_t scan = line.find("$$", i + 2);
           scan != std::string_view::npos; scan = line.find("$$", scan + 1)) {
        if (!IsEscapedAt(line, scan)) {
          close = scan;
          break;
        }
      }
      if (close != std::string_view::npos && close > i + 2) {
        segments->push_back(
            {std::string(line.substr(i + 2, close - i - 2)), true});
        out.append(MathPlaceholder(salt, segments->size() - 1));
        i = close + 2;
        continue;
      }
      out.append("$$");
      i += 2;
      continue;
    }
    std::size_t close = std::string_view::npos;
    if (i + 1 < line.size() && !IsAsciiSpace(line[i + 1])) {
      for (std::size_t j = i + 1; j < line.size(); ++j) {
        if (line[j] != '$' || IsAsciiSpace(line[j - 1]) ||
            IsEscapedAt(line, j)) {
          continue;
        }
        if (j + 1 < line.size() &&
            std::isdigit(static_cast<unsigned char>(line[j + 1])) != 0) {
          break;
        }
        close = j;
        break;
      }
    }
    if (close != std::string_view::npos) {
      segments->push_back(
          {std::string(line.substr(i + 1, close - i - 1)), false});
      out.append(MathPlaceholder(salt, segments->size() - 1));
      i = close + 1;
      continue;
    }
    out.push_back('$');
    ++i;
  }
  return out;
}

bool IsAtxHeading(std::string_view trimmed) {
  std::size_t hashes = 0;
  while (hashes < trimmed.size() && trimmed[hashes] == '#') {
    ++hashes;
  }
  if (hashes < 1 || hashes > 6) {
    return false;
  }
  return hashes == trimmed.size() || IsAsciiSpace(trimmed[hashes]);
}

bool IsThematicBreak(std::string_view trimmed) {
  if (trimmed.empty()) {
    return false;
  }
  const char marker = trimmed[0];
  if (marker != '*' && marker != '-' && marker != '_') {
    return false;
  }
  int count = 0;
  for (char c : trimmed) {
    if (c == marker) {
      ++count;
    } else if (!IsAsciiSpace(c)) {
      return false;
    }
  }
  return count >= 3;
}

bool IsSetextUnderline(std::string_view trimmed) {
  if (trimmed.empty()) {
    return false;
  }
  const char marker = trimmed[0];
  if (marker != '=' && marker != '-') {
    return false;
  }
  for (char c : trimmed) {
    if (c != marker) {
      return false;
    }
  }
  return true;
}

bool StripBlockQuoteMarker(std::string_view trimmed, std::string* rest) {
  if (!StartsWith(trimmed, ">")) {
    return false;
  }
  std::size_t i = 0;
  while (i < trimmed.size() && trimmed[i] == '>') {
    ++i;
    if (i < trimmed.size() && IsAsciiSpace(trimmed[i])) {
      ++i;
    }
  }
  *rest = std::string(trimmed.substr(i));
  return true;
}

bool EndsParagraph(std::string_view trimmed, bool paragraph_open) {
  if (IsAtxHeading(trimmed) || IsThematicBreak(trimmed)) {
    return true;
  }
  if (paragraph_open && IsSetextUnderline(trimmed)) {
    return true;
  }
  std::string quote_rest;
  if (StripBlockQuoteMarker(trimmed, &quote_rest)) {
    const std::string_view inner = TrimView(quote_rest);
    return inner.empty() || IsAtxHeading(inner) || IsThematicBreak(inner) ||
           (paragraph_open && IsSetextUnderline(inner));
  }
  return false;
}

std::string ExtractMath(std::string_view markdown,
                        std::vector<MdMathSegment>* segments,
                        std::string_view salt) {
  const std::vector<std::string> lines = SplitLines(markdown);
  std::vector<std::string> result;
  result.reserve(lines.size());

  bool in_fence = false;
  char fence_open_char = '\0';
  std::size_t fence_open_len = 0;
  std::string display_block;
  bool in_display_block = false;
  bool in_indent_code = false;
  bool paragraph_open = false;

  for (const std::string& line : lines) {
    const std::string_view trimmed = TrimView(line);
    const bool blank = trimmed.empty();
    const bool indented = StartsWith(line, "    ") || StartsWith(line, "\t");

    if (!in_display_block && !in_fence && !in_indent_code && indented &&
        !paragraph_open) {
      in_indent_code = true;
    }
    if (in_indent_code) {
      if (indented || blank) {
        result.push_back(line);
        paragraph_open = false;
        continue;
      }
      in_indent_code = false;
    }

    char fence_char = '\0';
    std::size_t fence_run = 0;
    if (!trimmed.empty() && (trimmed[0] == '`' || trimmed[0] == '~')) {
      fence_char = trimmed[0];
      while (fence_run < trimmed.size() && trimmed[fence_run] == fence_char) {
        ++fence_run;
      }
    }
    if (!in_display_block && !indented && fence_run >= 3) {
      if (!in_fence) {
        in_fence = true;
        fence_open_char = fence_char;
        fence_open_len = fence_run;
      } else if (fence_char == fence_open_char && fence_run >= fence_open_len &&
                 TrimView(trimmed.substr(fence_run)).empty()) {
        in_fence = false;
      }
      result.push_back(line);
      paragraph_open = false;
      continue;
    }
    if (in_fence) {
      result.push_back(line);
      continue;
    }

    if (in_display_block) {
      if (trimmed == "$$") {
        segments->push_back({display_block, true});
        in_display_block = false;
        display_block.clear();
        result.push_back(MathPlaceholder(salt, segments->size() - 1));
        paragraph_open = false;
      } else {
        display_block += line;
        display_block.push_back('\n');
      }
      continue;
    }
    if (trimmed == "$$") {
      in_display_block = true;
      display_block.clear();
      paragraph_open = false;
      continue;
    }

    result.push_back(ExtractMathInLine(line, segments, salt));
    paragraph_open = !blank && !EndsParagraph(trimmed, paragraph_open);
  }

  if (in_display_block) {
    result.push_back("$$");
    result.push_back(Trim(display_block));
    result.push_back("$$");
  }

  std::string joined;
  for (std::size_t i = 0; i < result.size(); ++i) {
    if (i != 0) {
      joined.push_back('\n');
    }
    joined += result[i];
  }
  return joined;
}

void AppendHtml(const MD_CHAR* text, MD_SIZE size, void* userdata) {
  auto* out = static_cast<std::string*>(userdata);
  out->append(text, static_cast<std::size_t>(size));
}

std::string LanguageFromAttrs(std::string_view attrs) {
  constexpr std::string_view kClass = "class=\"language-";
  const std::size_t start = attrs.find(kClass);
  if (start == std::string_view::npos) {
    return {};
  }
  const std::size_t value_start = start + kClass.size();
  const std::size_t end = attrs.find('"', value_start);
  if (end == std::string_view::npos) {
    return {};
  }
  return std::string(attrs.substr(value_start, end - value_start));
}

struct CodeBlockSpan {
  std::size_t tag_start = 0;
  std::size_t content_start = 0;
  std::size_t content_end = 0;
  std::size_t tag_end = 0;
  std::string attrs;
};

bool FindNextCodeBlock(std::string_view html, std::size_t from,
                       CodeBlockSpan* span) {
  constexpr std::string_view kOpen = "<pre><code";
  while (from < html.size()) {
    const std::size_t start = html.find(kOpen, from);
    if (start == std::string_view::npos) {
      return false;
    }
    if (start != 0 && html[start - 1] != '\n') {
      from = start + 1;
      continue;
    }
    // The first '>' closes <pre>; the code tag's '>' comes after <code.
    const std::size_t open_end = html.find('>', start + kOpen.size());
    if (open_end == std::string_view::npos) {
      return false;
    }
    const std::size_t close = html.find("</code></pre>", open_end + 1);
    if (close == std::string_view::npos) {
      return false;
    }
    span->tag_start = start;
    span->content_start = open_end + 1;
    span->content_end = close;
    span->tag_end = close + 13;
    span->attrs = std::string(
        html.substr(start + kOpen.size(), open_end - start - kOpen.size()));
    return true;
  }
  return false;
}

std::vector<MdCodeBlock> CollectCodeBlocks(std::string_view html) {
  std::vector<MdCodeBlock> blocks;
  std::size_t pos = 0;
  int index = 0;
  CodeBlockSpan span;
  while (FindNextCodeBlock(html, pos, &span)) {
    MdCodeBlock block;
    block.index = index++;
    block.language = LanguageFromAttrs(span.attrs);
    block.code = UnescapeHtml(
        html.substr(span.content_start, span.content_end - span.content_start));
    blocks.push_back(std::move(block));
    pos = span.tag_end;
  }
  return blocks;
}

bool StartsWithFrontMatter(std::string_view markdown) {
  if (markdown.size() < 4 || markdown.substr(0, 3) != "---") {
    return false;
  }
  return markdown[3] == '\n' ||
         (markdown[3] == '\r' && markdown.size() > 4 && markdown[4] == '\n');
}

bool IsFrontMatterClose(std::string_view line) {
  if (!line.empty() && line.back() == '\r') {
    line.remove_suffix(1);
  }
  const std::string_view trimmed = TrimView(line);
  return trimmed == "---" || trimmed == "...";
}

void ParseFrontMatterEntries(std::string_view block,
                             std::vector<MdFrontMatterEntry>* entries) {
  std::size_t pos = 0;
  while (pos <= block.size()) {
    std::size_t end = block.find('\n', pos);
    if (end == std::string_view::npos) {
      end = block.size();
    }
    std::string_view line = block.substr(pos, end - pos);
    if (!line.empty() && line.back() == '\r') {
      line.remove_suffix(1);
    }
    line = TrimView(line);
    if (!line.empty() && line[0] != '#') {
      const std::size_t colon = line.find(':');
      if (colon != std::string_view::npos) {
        std::string_view key = TrimView(line.substr(0, colon));
        std::string_view value = TrimView(line.substr(colon + 1));
        if (value.size() >= 2 &&
            (value.front() == '"' || value.front() == '\'') &&
            value.back() == value.front()) {
          value = value.substr(1, value.size() - 2);
        }
        if (!key.empty()) {
          entries->push_back({std::string(key), std::string(value)});
        }
      }
    }
    if (end == block.size()) {
      break;
    }
    pos = end + 1;
  }
}

// Front matter is only recognized at the very first byte. A rule later in the
// document stays a thematic break, and an unclosed block falls back to normal
// Markdown so a document beginning with "---" is never swallowed.
std::string_view StripFrontMatter(std::string_view markdown,
                                  std::vector<MdFrontMatterEntry>* entries) {
  if (!StartsWithFrontMatter(markdown)) {
    return markdown;
  }
  const std::size_t first_end = markdown.find('\n');
  std::size_t pos = first_end + 1;
  int lines = 0;
  while (pos <= markdown.size() && lines < 30) {
    std::size_t end = markdown.find('\n', pos);
    if (end == std::string_view::npos) {
      end = markdown.size();
    }
    const std::string_view line = markdown.substr(pos, end - pos);
    if (IsFrontMatterClose(line)) {
      ParseFrontMatterEntries(
          markdown.substr(first_end + 1, pos - first_end - 1), entries);
      return markdown.substr(end < markdown.size() ? end + 1 : markdown.size());
    }
    ++lines;
    if (end == markdown.size()) {
      break;
    }
    pos = end + 1;
  }
  entries->clear();
  return markdown;
}

}  // namespace

MdParseResult ParseMarkdown(std::string_view markdown, bool extract_math,
                            MdAdmonitionMode admonition_mode) {
  MdParseResult parsed;
  parsed.math_salt = RandomHexSalt(8);
  const std::string_view body =
      StripFrontMatter(markdown, &parsed.front_matter);
  std::string source =
      extract_math ? ExtractMath(body, &parsed.segments, parsed.math_salt)
                   : std::string(body);

  // Superscript/subscript stay off: with formula extraction disabled, md4c
  // pairs the carets around "$x^2$" and eats the literal TeX. Highlight and
  // insert have no such conflict with the math syntax.
  unsigned flags = MD_DIALECT_GITHUB | MD_FLAG_HIGHLIGHT | MD_FLAG_INSERT;
  if (admonition_mode == MdAdmonitionMode::kMpe) {
    // MPE callouts are rewritten from blockquotes below; letting md4c
    // consume marker-only admonitions here would split the two paths.
    flags &= ~MD_FLAG_ADMONITIONS;
  }

  MdBlockCollectOptions block_options;
  block_options.source = source;
  block_options.flags = flags;
  block_options.admonition_mode = admonition_mode;
  block_options.math_segments = &parsed.segments;
  block_options.math_salt = parsed.math_salt;

  // The block pass re-parses the same source. It is independent of md_html,
  // so run it concurrently; a thread failure falls back to the same work
  // sequentially. md4c is reentrant and both passes own their buffers.
  std::vector<MdBlock> collected;
  std::thread collector;
  bool collector_started = false;
  try {
    collector = std::thread([&collected, &block_options] {
      try {
        collected = CollectDocumentBlocks(block_options);
      } catch (...) {
      }
    });
    collector_started = true;
  } catch (...) {
  }

  const int result =
      md_html(source.data(), static_cast<MD_SIZE>(source.size()), AppendHtml,
              &parsed.html, flags, MD_HTML_FLAG_SKIP_UTF8_BOM);
  if (collector_started) {
    collector.join();
  } else {
    try {
      collected = CollectDocumentBlocks(block_options);
    } catch (...) {
    }
  }
  parsed.document_blocks = std::move(collected);

  if (result != 0) {
    parsed.html = "<p class=\"error\">Markdown 解析失败</p>";
    parsed.parse_error = true;
    return parsed;
  }
  if (admonition_mode == MdAdmonitionMode::kMpe) {
    parsed.html = ApplyMpeCallouts(parsed.html);
  }
  parsed.blocks = CollectCodeBlocks(parsed.html);
  return parsed;
}
