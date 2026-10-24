#include "md_assembler.h"

#include <unordered_set>
#include <utility>

#include "md_text.h"

namespace {

void SplitCodeLines(const std::string& highlighted,
                    std::vector<std::string>* lines) {
  if (highlighted.empty()) {
    return;
  }
  std::string body = highlighted;
  if (!body.empty() && body.back() == '\n') {
    body.pop_back();
  }
  std::size_t start = 0;
  while (start <= body.size()) {
    const std::size_t newline = body.find('\n', start);
    const std::size_t end =
        newline == std::string::npos ? body.size() : newline;
    lines->push_back(body.substr(start, end - start));
    if (newline == std::string::npos) {
      break;
    }
    start = newline + 1;
  }
}

std::string BuildCodeBlockFrame(const std::string& highlighted,
                                const std::string& language,
                                const std::string& block_id,
                                bool line_numbers) {
  const std::string lang_text = language.empty() ? "text" : language;
  std::string out = "<div class=\"code-wrap\">";
  out += "<div class=\"code-header\">";
  out += "<span class=\"code-lang\">";
  out += EscapeHtml(lang_text);
  out += "</span>";
  out += "<a class=\"copy-btn\" href=\"copy:";
  out += block_id;
  out += "\">复制</a>";
  out += "</div>";
  if (line_numbers) {
    out += "<table class=\"md-code\"><tbody>";
    std::vector<std::string> lines;
    SplitCodeLines(highlighted, &lines);
    for (std::size_t i = 0; i < lines.size(); ++i) {
      out += "<tr><td class=\"md-code-ln\">";
      out += std::to_string(i + 1);
      out += "</td><td class=\"md-code-line\">";
      out += lines[i].empty() ? "&#160;" : lines[i];
      out += "</td></tr>";
    }
    out += "</tbody></table>";
  } else if (language.empty()) {
    out += "<pre><code>";
    out += highlighted;
    out += "</code></pre>";
  } else {
    out += "<pre><code class=\"language-";
    out += EscapeHtml(language);
    out += "\">";
    out += highlighted;
    out += "</code></pre>";
  }
  out += "</div>";
  return out;
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

std::string HighlightCodeBlocks(std::string_view html,
                                const MdMermaidSink& mermaid,
                                const MdCodeBlockSink& code_blocks,
                                const MdAssembleOptions& options) {
  std::string result;
  result.reserve(html.size());
  std::size_t consumed = 0;
  int block_index = 0;
  CodeBlockSpan span;
  while (FindNextCodeBlock(html, consumed, &span)) {
    result.append(html.substr(consumed, span.tag_start - consumed));
    consumed = span.tag_end;

    const std::string language = LanguageFromAttrs(span.attrs);
    const std::string code = UnescapeHtml(
        html.substr(span.content_start, span.content_end - span.content_start));

    if (mermaid && language == "mermaid") {
      const std::string image = mermaid(code);
      if (!image.empty()) {
        result += "<div class=\"mermaid-wrap\">";
        result += image;
        result += "</div>";
        continue;
      }
    }

    const std::string block_id = std::to_string(block_index++);
    if (code_blocks) {
      code_blocks(block_id, code);
    }
    std::string highlighted = options.highlight
                                  ? options.highlight(code, language)
                                  : EscapeHtml(code);
    if (highlighted.empty() && !code.empty()) {
      highlighted = EscapeHtml(code);
    }
    result += BuildCodeBlockFrame(highlighted, language, block_id,
                                  options.code_line_numbers);
  }
  result.append(html.substr(consumed));
  return result;
}

std::string RestoreMath(std::string_view html,
                        const std::vector<MdMathSegment>& segments,
                        const MdFormulaSink& formula, std::string_view salt) {
  std::string prefix = "@@mj-";
  prefix.append(salt);
  prefix.push_back('-');
  std::string result;
  result.reserve(html.size());
  std::size_t consumed = 0;
  while (consumed < html.size()) {
    const std::size_t start = html.find(prefix, consumed);
    if (start == std::string_view::npos) {
      result.append(html.substr(consumed));
      break;
    }
    std::size_t digits_end = start + prefix.size();
    while (digits_end < html.size() && html[digits_end] >= '0' &&
           html[digits_end] <= '9') {
      ++digits_end;
    }
    if (digits_end + 2 > html.size() || html.substr(digits_end, 2) != "@@" ||
        digits_end == start + prefix.size()) {
      result.append(html.substr(consumed, start + prefix.size() - consumed));
      consumed = start + prefix.size();
      continue;
    }
    result.append(html.substr(consumed, start - consumed));
    std::size_t index = 0;
    bool index_ok = true;
    for (std::size_t i = start + prefix.size(); i < digits_end; ++i) {
      if (index > (static_cast<std::size_t>(-1) - 9) / 10) {
        index_ok = false;
        break;
      }
      index = index * 10 + static_cast<std::size_t>(html[i] - '0');
    }
    if (index_ok && index < segments.size()) {
      const MdMathSegment& segment = segments[index];
      const std::string rendered =
          formula ? formula(segment.tex, segment.display) : std::string();
      if (!rendered.empty()) {
        result += rendered;
      } else {
        const std::string source = segment.display ? "$$" + segment.tex + "$$"
                                                   : "$" + segment.tex + "$";
        result += "<code class=\"math-src\">";
        result += EscapeHtml(source);
        result += "</code>";
      }
    }
    consumed = digits_end + 2;
  }
  return result;
}

std::string StripTags(std::string_view inner) {
  std::string out;
  out.reserve(inner.size());
  std::size_t i = 0;
  while (i < inner.size()) {
    if (inner[i] == '<') {
      const std::size_t close = inner.find('>', i + 1);
      if (close == std::string_view::npos) {
        out.append(inner.substr(i));
        break;
      }
      i = close + 1;
      continue;
    }
    out.push_back(inner[i]);
    ++i;
  }
  return out;
}

std::string AddHeadingIds(
    std::string_view html, const MdHeadingSink& on_heading,
    const std::function<void(const MdHeading&)>& on_outline) {
  std::string result;
  result.reserve(html.size());
  std::size_t consumed = 0;
  int index = 0;
  std::unordered_set<std::string> used;
  while (consumed < html.size()) {
    const std::size_t start = html.find("<h", consumed);
    if (start == std::string_view::npos) {
      result.append(html.substr(consumed));
      break;
    }
    if (start + 3 >= html.size() || html[start + 2] < '1' ||
        html[start + 2] > '6' || html[start + 3] != '>') {
      result.append(html.substr(consumed, start + 2 - consumed));
      consumed = start + 2;
      continue;
    }
    const char level = html[start + 2];
    const std::string close_tag = std::string("</h") + level + ">";
    const std::size_t inner_start = start + 4;
    const std::size_t end = html.find(close_tag, inner_start);
    if (end == std::string_view::npos) {
      result.append(html.substr(consumed, start + 2 - consumed));
      consumed = start + 2;
      continue;
    }
    result.append(html.substr(consumed, start - consumed));
    const std::string inner(html.substr(inner_start, end - inner_start));
    const std::string plain = UnescapeHtml(StripTags(inner));
    const std::string base = Slugify(plain);
    std::string slug = base;
    int suffix = 0;
    while (used.count(slug) != 0) {
      ++suffix;
      slug = base + "-" + std::to_string(suffix);
    }
    used.insert(slug);
    const std::string ascii_id = "h-" + std::to_string(index++);
    if (on_heading) {
      on_heading(slug, ascii_id);
    }
    if (on_outline) {
      MdHeading heading;
      heading.level = level - '0';
      heading.text = plain;
      heading.anchor = slug;
      on_outline(heading);
    }
    result += "<h";
    result += level;
    result += " id=\"";
    result += ascii_id;
    result += "\" data-md-slug=\"";
    result += EscapeHtml(slug);
    result += "\">";
    result += inner;
    result += "</h";
    result += level;
    result += ">";
    consumed = end + close_tag.size();
  }
  return result;
}

}  // namespace

std::string AssembleMarkdown(const MdParseResult& parsed,
                             const MdRenderResolver& resolver,
                             const MdCodeBlockSink& code_blocks,
                             const MdHeadingSink& heading_ids,
                             const MdAssembleOptions& options) {
  if (parsed.parse_error) {
    return parsed.html;
  }
  MdAssembleOptions resolved = options;
  resolved.highlight = [&resolver, &options](std::string_view code,
                                             std::string_view language) {
    if (options.highlight) {
      const std::string custom = options.highlight(code, language);
      if (!custom.empty()) {
        return custom;
      }
    }
    const std::string rendered =
        resolver(MdRenderKind::Highlight, code, language, false);
    return rendered.empty() && !code.empty() ? EscapeHtml(code) : rendered;
  };
  const MdMermaidSink mermaid = [&resolver](std::string_view code) {
    return resolver(MdRenderKind::Mermaid, code, std::string_view(), false);
  };
  const MdFormulaSink formula = [&resolver](std::string_view tex,
                                            bool display) {
    return resolver(MdRenderKind::Formula, tex, std::string_view(), display);
  };
  const std::string code_highlighted =
      HighlightCodeBlocks(parsed.html, mermaid, code_blocks, resolved);
  const std::string math_restored =
      RestoreMath(code_highlighted, parsed.segments, formula, parsed.math_salt);
  return AddHeadingIds(math_restored, heading_ids, options.outline);
}
