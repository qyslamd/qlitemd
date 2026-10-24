#include "md_highlight.h"

#include <string>
#include <thread>

#include <gtest/gtest.h>

namespace {

TEST(MdHighlightTest, ColorsCppTokens) {
  const std::string html = MdHighlightCode(
      "class A { int n = 42; const char* s = \"x\"; };  // note", "cpp",
      nullptr);
  EXPECT_NE(html.find("class=\"kw\""), std::string::npos) << html;
  EXPECT_NE(html.find("class=\"num\">42<"), std::string::npos) << html;
  EXPECT_NE(html.find("class=\"str\""), std::string::npos) << html;
  EXPECT_NE(html.find("class=\"cmt\""), std::string::npos) << html;
  EXPECT_NE(html.find("&quot;x&quot;"), std::string::npos) << html;
}

TEST(MdHighlightTest, CoversBundledLanguages) {
  const char* code = "x";
  for (const char* language :
       {"bash", "batch", "c", "cpp", "css", "go", "html", "java", "javascript",
        "json", "python", "rust"}) {
    const std::string html = MdHighlightCode(code, language, nullptr);
    EXPECT_FALSE(html.empty()) << language;
  }
}

TEST(MdHighlightTest, AliasesResolve) {
  const std::string python =
      MdHighlightCode("def f():\n    pass\n", "py", nullptr);
  EXPECT_NE(python.find("class=\"kw\""), std::string::npos) << python;

  const std::string cpp = MdHighlightCode("class A {};", "cpp", nullptr);
  for (const char* alias : {"c++", "cpp"}) {
    EXPECT_EQ(MdHighlightCode("class A {};", alias, nullptr), cpp) << alias;
  }
  EXPECT_EQ(MdHighlightCode("const a = 1;", "js", nullptr),
            MdHighlightCode("const a = 1;", "javascript", nullptr));
}

TEST(MdHighlightTest, MatchPredicateFiltersCaptures) {
  // identifier -> @constant carries #match? "^[A-Z][A-Z\d_]*$": the lowercase
  // identifier must keep no span, otherwise predicates were not evaluated.
  const std::string upper = MdHighlightCode("int MAX_SIZE = 10;", "c", nullptr);
  EXPECT_NE(upper.find("class=\"num\">MAX_SIZE<"), std::string::npos) << upper;
  const std::string lower = MdHighlightCode("int lower = 10;", "c", nullptr);
  EXPECT_EQ(lower.find("class=\"num\">lower<"), std::string::npos) << lower;
}

TEST(MdHighlightTest, UnknownLanguageIsPlainEscaped) {
  EXPECT_EQ(MdHighlightCode("a < b & c", "notalang", nullptr),
            "a &lt; b &amp; c");
  EXPECT_EQ(MdHighlightCode("a'b", "notalang", nullptr), "a&#x27;b");
}

TEST(MdHighlightTest, EmptyInputYieldsEmpty) {
  EXPECT_EQ(MdHighlightCode("", "cpp", nullptr), "");
}

TEST(MdHighlightTest, SpansAreBalancedPerLine) {
  const std::string html = MdHighlightCode(
      "/* first\nsecond */ int a = 1;\n// done\n", "cpp", nullptr);
  std::size_t start = 0;
  while (start <= html.size()) {
    const std::size_t newline = html.find('\n', start);
    const std::size_t end =
        newline == std::string::npos ? html.size() : newline;
    const std::string line = html.substr(start, end - start);
    std::size_t open = 0;
    std::size_t close = 0;
    for (std::size_t at = line.find("<span"); at != std::string::npos;
         at = line.find("<span", at + 1)) {
      ++open;
    }
    for (std::size_t at = line.find("</span>"); at != std::string::npos;
         at = line.find("</span>", at + 1)) {
      ++close;
    }
    EXPECT_EQ(open, close) << line;
    if (newline == std::string::npos) {
      break;
    }
    start = newline + 1;
  }
}

TEST(MdHighlightTest, RenderKeySeparatesLanguage) {
  EXPECT_EQ(MdHighlightRenderKey("int a = 1;", "cpp"), "cpp\nint a = 1;");
  EXPECT_NE(MdHighlightRenderKey("x", "cpp"),
            MdHighlightRenderKey("x", "python"));
}

TEST(MdHighlightTest, ParserCacheWorksOnWorkerThread) {
  std::string html;
  std::thread worker([&html] {
    MdTreeSitterParserCache cache;
    html = MdHighlightCode("int a = 1;", "cpp", &cache);
  });
  worker.join();
  EXPECT_NE(html.find("<span"), std::string::npos) << html;
}

TEST(MdHighlightTest, ParserCacheReusesParsers) {
  MdTreeSitterParserCache cache;
  const std::string first = MdHighlightCode("int a = 1;", "cpp", &cache);
  const std::string second = MdHighlightCode("int a = 1;", "cpp", &cache);
  EXPECT_EQ(first, second);
  EXPECT_NE(first.find("<span"), std::string::npos) << first;
}

}  // namespace
