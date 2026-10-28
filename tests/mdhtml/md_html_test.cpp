#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "md_assembler.h"
#include "md_parser.h"
#include "md_text.h"

namespace {

std::string ResolveNothing(MdRenderKind kind, std::string_view payload,
                           std::string_view language, bool display) {
  (void)kind;
  (void)payload;
  (void)language;
  (void)display;
  return {};
}

std::string EscapeOnly(MdRenderKind kind, std::string_view payload,
                       std::string_view language, bool display) {
  (void)language;
  (void)display;
  if (kind == MdRenderKind::Highlight) {
    return EscapeHtml(payload);
  }
  return {};
}

std::string Assemble(const MdParseResult& parsed, MdRenderResolver resolver) {
  return AssembleMarkdown(parsed, resolver, {}, {}, {});
}

}  // namespace

TEST(MdHtmlTest, HeadingsGetSlugAndAsciiId) {
  const MdParseResult parsed =
      ParseMarkdown("# Hello World\n\n## Hello World\n", false);
  const std::string html = Assemble(parsed, ResolveNothing);
  EXPECT_NE(html.find("<h1 id=\"h-0\" data-md-slug=\"hello-world\">"),
            std::string::npos);
  EXPECT_NE(html.find("<h2 id=\"h-1\" data-md-slug=\"hello-world-1\">"),
            std::string::npos);
}

TEST(MdHtmlTest, CjkHeadingKeepsCharactersInSlug) {
  const MdParseResult parsed = ParseMarkdown("## 中文标题\n", false);
  const std::string html = Assemble(parsed, ResolveNothing);
  EXPECT_NE(html.find("data-md-slug=\"中文标题\""), std::string::npos);
}

TEST(MdHtmlTest, GfmTableAndTaskListSurvive) {
  const MdParseResult parsed = ParseMarkdown(
      "| a | b |\n|---|---|\n| 1 | 2 |\n\n- [x] done\n- [ ] todo\n", false);
  const std::string html = Assemble(parsed, ResolveNothing);
  EXPECT_NE(html.find("<table>"), std::string::npos);
  EXPECT_NE(html.find("type=\"checkbox\""), std::string::npos);
}

TEST(MdHtmlTest, GithubAdmonitionGetsClassAndTitle) {
  const MdParseResult parsed = ParseMarkdown("> [!IMPORTANT]\n> Read this.\n",
                                             false, MdAdmonitionMode::kGithub);
  const std::string html = Assemble(parsed, ResolveNothing);
  EXPECT_NE(html.find("<div class=\"admonition-important\">"),
            std::string::npos);
  EXPECT_NE(html.find("<p class=\"admonition-title\">important</p>"),
            std::string::npos);
  EXPECT_NE(html.find("<p>Read this.</p>"), std::string::npos);
  EXPECT_EQ(html.find("[!IMPORTANT]"), std::string::npos);
}

TEST(MdHtmlTest, GithubAdmonitionMarkerMustBeOnItsOwnLine) {
  const MdParseResult parsed = ParseMarkdown("> [!IMPORTANT] inline text\n",
                                             false, MdAdmonitionMode::kGithub);
  const std::string html = Assemble(parsed, ResolveNothing);
  EXPECT_NE(html.find("<blockquote>"), std::string::npos);
  EXPECT_NE(html.find("[!IMPORTANT] inline text"), std::string::npos);
  EXPECT_EQ(html.find("admonition-important"), std::string::npos);
}

TEST(MdHtmlTest, MpeCalloutUsesInlineTextAsTitle) {
  const MdParseResult parsed = ParseMarkdown("> [!note] Inline title\n", false);
  const std::string html = Assemble(parsed, ResolveNothing);
  EXPECT_NE(html.find("<div class=\"callout callout-note\">"),
            std::string::npos);
  EXPECT_NE(html.find("<span class=\"callout-icon\">"), std::string::npos);
  EXPECT_NE(html.find("</span>Inline title</div>"), std::string::npos);
  EXPECT_EQ(html.find("[!note]"), std::string::npos);
}

TEST(MdHtmlTest, MpeCalloutDefaultsTitleAndKeepsBody) {
  const MdParseResult parsed =
      ParseMarkdown("> [!important]\n> Read this.\n", false);
  const std::string html = Assemble(parsed, ResolveNothing);
  EXPECT_NE(html.find("<div class=\"callout callout-important\">"),
            std::string::npos);
  EXPECT_NE(html.find("</span>Important</div>"), std::string::npos);
  EXPECT_NE(html.find("<p>Read this.</p>"), std::string::npos);
}

TEST(MdHtmlTest, MpeCalloutSupportsInfoType) {
  const MdParseResult parsed = ParseMarkdown("> [!info] A note\n", false);
  const std::string html = Assemble(parsed, ResolveNothing);
  EXPECT_NE(html.find("<div class=\"callout callout-info\">"),
            std::string::npos);
  EXPECT_NE(html.find("</span>A note</div>"), std::string::npos);
}

TEST(MdHtmlTest, MpeCalloutKeepsFollowingBlocks) {
  const MdParseResult parsed =
      ParseMarkdown("> [!tip] Tips\n> - one\n> - two\n", false);
  const std::string html = Assemble(parsed, ResolveNothing);
  EXPECT_NE(html.find("<div class=\"callout callout-tip\">"),
            std::string::npos);
  EXPECT_NE(html.find("</span>Tips</div>"), std::string::npos);
  EXPECT_NE(html.find("<ul>"), std::string::npos);
}

TEST(MdHtmlTest, MpeCalloutLeavesUnknownTypeAlone) {
  const MdParseResult parsed = ParseMarkdown("> [!unknown] text\n", false);
  const std::string html = Assemble(parsed, ResolveNothing);
  EXPECT_NE(html.find("<blockquote>"), std::string::npos);
  EXPECT_NE(html.find("[!unknown] text"), std::string::npos);
  EXPECT_EQ(html.find("callout"), std::string::npos);
}

TEST(MdHtmlTest, GfmFootnoteGetsSectionAndBackref) {
  const MdParseResult parsed =
      ParseMarkdown("Text[^1].\n\n[^1]: Note.\n", false);
  const std::string html = Assemble(parsed, ResolveNothing);
  EXPECT_NE(html.find("<sup><a href=\"#fn-1\" id=\"fnref-1-1\">1</a></sup>"),
            std::string::npos);
  EXPECT_NE(html.find("<section class=\"footnotes\">"), std::string::npos);
  EXPECT_NE(html.find("id=\"fn-1\""), std::string::npos);
  EXPECT_NE(html.find("class=\"footnote-backref\""), std::string::npos);
}

TEST(MdHtmlTest, MathSegmentsAndRestore) {
  const MdParseResult parsed =
      ParseMarkdown("Inline $x^2$ and display $$y$$.\n", true);
  ASSERT_EQ(parsed.segments.size(), 2u);
  EXPECT_EQ(parsed.segments[0].tex, "x^2");
  EXPECT_FALSE(parsed.segments[0].display);
  EXPECT_EQ(parsed.segments[1].tex, "y");
  EXPECT_TRUE(parsed.segments[1].display);

  const std::string html =
      Assemble(parsed, [](MdRenderKind kind, std::string_view payload,
                          std::string_view language, bool display) {
        (void)language;
        if (kind == MdRenderKind::Formula) {
          return "<img data-tex=\"" + EscapeHtml(payload) +
                 "\" data-display=\"" + (display ? "1" : "0") + "\">";
        }
        return std::string();
      });
  EXPECT_NE(html.find("data-tex=\"x^2\""), std::string::npos);
  EXPECT_NE(html.find("data-tex=\"y\""), std::string::npos);
}

TEST(MdHtmlTest, EscapedDollarIsNotMath) {
  const MdParseResult parsed =
      ParseMarkdown("price \\$5 and code `$x$`\n", true);
  EXPECT_TRUE(parsed.segments.empty());
  const std::string html = Assemble(parsed, ResolveNothing);
  EXPECT_NE(html.find("$5"), std::string::npos);
  EXPECT_NE(html.find("$x$"), std::string::npos);
}

TEST(MdHtmlTest, CodeBlockGetsFrameAndCopyId) {
  const MdParseResult parsed =
      ParseMarkdown("```cpp\nint x = 1;\n```\n", false);
  ASSERT_EQ(parsed.blocks.size(), 1u);
  EXPECT_EQ(parsed.blocks[0].language, "cpp");
  EXPECT_EQ(parsed.blocks[0].code, "int x = 1;\n");

  std::string copied_id;
  std::string copied_language;
  std::string copied_code;
  MdAssembleOptions options;
  options.highlight = [](std::string_view code, std::string_view language) {
    (void)language;
    return "<b>" + EscapeHtml(code) + "</b>";
  };
  const std::string html = AssembleMarkdown(
      parsed, ResolveNothing,
      [&](std::string_view id, std::string_view language,
          std::string_view code) {
        copied_id = id;
        copied_language = language;
        copied_code = code;
      },
      {}, options);
  EXPECT_EQ(copied_id, "0");
  EXPECT_EQ(copied_code, "int x = 1;\n");
  EXPECT_NE(html.find("<span class=\"code-lang\">cpp</span>"),
            std::string::npos);
  EXPECT_NE(html.find("href=\"copy:0\""), std::string::npos);
  EXPECT_NE(html.find("<b>int x = 1;"), std::string::npos);
}

TEST(MdHtmlTest, CodeBlockDecodesMd4cSingleQuoteEntity) {
  const MdParseResult parsed =
      ParseMarkdown("```cpp\nchar c = 'x';\n```\n", false);
  const std::string html = Assemble(parsed, EscapeOnly);
  EXPECT_NE(html.find("char c = 'x';"), std::string::npos);
  EXPECT_EQ(html.find("&#x27;"), std::string::npos);
}

TEST(MdHtmlTest, MermaidSinkAndFallback) {
  const MdParseResult parsed =
      ParseMarkdown("```mermaid\ngraph TD; A-->B;\n```\n", false);
  const std::string rendered =
      Assemble(parsed, [](MdRenderKind kind, std::string_view payload,
                          std::string_view language, bool display) {
        (void)language;
        (void)display;
        if (kind == MdRenderKind::Mermaid) {
          return "<img src=\"asset:0\" data-src=\"" + EscapeHtml(payload) +
                 "\">";
        }
        return std::string();
      });
  EXPECT_NE(rendered.find("mermaid-wrap"), std::string::npos);
  EXPECT_NE(rendered.find("data-src=\"graph TD; A--&gt;B;\n\""),
            std::string::npos);

  const std::string fallback = Assemble(parsed, EscapeOnly);
  EXPECT_NE(fallback.find("code-lang\">mermaid"), std::string::npos);
  EXPECT_EQ(fallback.find("mermaid-wrap"), std::string::npos);
}

TEST(MdHtmlTest, TextHelpers) {
  EXPECT_EQ(Trim("  a \n"), "a");
  EXPECT_EQ(Slugify("Hello 世界!"), "hello-世界");
  EXPECT_EQ(EscapeHtml("<a&\"b\">"), "&lt;a&amp;&quot;b&quot;&gt;");
  EXPECT_EQ(UnescapeHtml("&lt;a&amp;b&gt;"), "<a&b>");
  EXPECT_EQ(UnescapeHtml("&amp;lt;"), "&lt;");
}
