#include "code_highlighter.h"

#include <gtest/gtest.h>
#include <thread>

#include <QString>
#include <QStringList>

#include "markdown_engine_state.h"
#include "markdown_render_context.h"

// 高亮的端到端判据：qrc 载荷 → QuickJS 里跑 highlight.js → 类名映射后的
// HTML。类名（kw/str/num/cmt/…）是与主题样式表的唯一契约，这里
// 钉的是"关键字/字符串有没有被认出来""每行的 span 是否配平"这类静默出错的
// 地方；配色本身是 CSS 的事
namespace {

TEST(CodeHighlighterTest, ColorsCppKeywordsStringsNumbersAndComments) {
  const QString html = HighlightCode(
      QStringLiteral(
          "class A { int n = 42; const char* s = \"x\"; };  // 说明"),
      QStringLiteral("cpp"));
  EXPECT_TRUE(html.contains(QStringLiteral("class=\"kw\"")))
      << html.toStdString();
  EXPECT_TRUE(html.contains(QStringLiteral("class=\"num\"")))
      << html.toStdString();
  EXPECT_TRUE(html.contains(QStringLiteral("class=\"str\"")))
      << html.toStdString();
  EXPECT_TRUE(html.contains(QStringLiteral("class=\"cmt\"")))
      << html.toStdString();
  // 内容还在（转义过的）
  EXPECT_TRUE(html.contains(QStringLiteral("42"))) << html.toStdString();
}

// 围栏标签到 hljs 语言的别名（py→python、bat→dos…）要在入口里处理
TEST(CodeHighlighterTest, LanguageAliasesResolve) {
  const QString py = HighlightCode(QStringLiteral("def f():\n    pass\n"),
                                   QStringLiteral("py"));
  EXPECT_TRUE(py.contains(QStringLiteral("class=\"kw\""))) << py.toStdString();

  const QString bat =
      HighlightCode(QStringLiteral("echo hello\n"), QStringLiteral("bat"));
  EXPECT_FALSE(bat.isEmpty()) << bat.toStdString();
}

// 未注册的语言不猜（上游示例会 highlightAuto，这里有意关掉：猜错的配色比
// 不高亮更难看）——返回纯转义的原文，一个 span 都没有
TEST(CodeHighlighterTest, UnknownLanguageFallsBackToPlainEscape) {
  const QString html =
      HighlightCode(QStringLiteral("a < b & c"), QStringLiteral("notalang"));
  EXPECT_EQ(html, QStringLiteral("a &lt; b &amp; c")) << html.toStdString();
  EXPECT_FALSE(html.contains(QStringLiteral("<span"))) << html.toStdString();
}

// 空输入返回空串（markdown_to_html 对空代码块本来就不该走到这里）
TEST(CodeHighlighterTest, EmptyInputYieldsEmpty) {
  EXPECT_EQ(HighlightCode(QString(), QStringLiteral("cpp")), QString());
}

// 跨行 span 拆行：C++ 侧按 \n 处理代码块时，任何一行的 <span 与 </span>
// 数量都必须相等，否则 HTML 标签配对会断（splitSpansPerLine 的职责）
TEST(CodeHighlighterTest, SpansAreBalancedOnEveryLine) {
  const QString html = HighlightCode(
      QStringLiteral("/* 第一行\n第二行 */ int a = 1;\n// done\n"),
      QStringLiteral("cpp"));
  const QStringList lines = html.split(QChar('\n'));
  ASSERT_GT(lines.size(), 1);
  for (const QString& line : lines) {
    const int open = line.count(QStringLiteral("<span"));
    const int close = line.count(QStringLiteral("</span>"));
    EXPECT_EQ(open, close) << line.toStdString();
  }
}

// 输出里的类名必须都是 CSS 认识的短类名（hljs-xxx 前缀如果漏映射，样式就
// 静默丢失——配色看着"对了一半"最难查）
TEST(CodeHighlighterTest, NoUnmappedHljsClassNamesRemain) {
  const QString html = HighlightCode(
      QStringLiteral("#include <vector>\nstruct S { double x; };\n"),
      QStringLiteral("cpp"));
  EXPECT_FALSE(html.contains(QStringLiteral("hljs-"))) << html.toStdString();
}

// 同一语言 + 源码第二次命中内存缓存，不再进 QuickJS；语言参与 key
TEST(CodeHighlighterTest, CacheHitSkipsSecondRender) {
  MarkdownRenderContext context;
  const QString code = QStringLiteral("int a = 1;");
  const QString first = HighlightCode(context, code, QStringLiteral("cpp"));
  const QString second = HighlightCode(context, code, QStringLiteral("cpp"));
  HighlightCode(context, code, QStringLiteral("python"));

  EXPECT_EQ(first, second);
  const MarkdownContextStatus status = context.status().highlight_context;
  EXPECT_EQ(status.renders, 2u);
  EXPECT_EQ(status.memory_cache_misses, 2u);
  EXPECT_EQ(status.memory_cache_hits, 1u);
}

// 未注册语言的转义兜底也进缓存；第二次不再进 QuickJS，且不算 failure
TEST(CodeHighlighterTest, UnknownLanguageFallbackIsCached) {
  MarkdownRenderContext context;
  const QString code = QStringLiteral("a < b & c");
  const QString first =
      HighlightCode(context, code, QStringLiteral("notalang"));
  const QString second =
      HighlightCode(context, code, QStringLiteral("notalang"));

  EXPECT_EQ(first, QStringLiteral("a &lt; b &amp; c"));
  EXPECT_EQ(second, first);
  const MarkdownContextStatus status = context.status().highlight_context;
  EXPECT_EQ(status.renders, 1u);
  EXPECT_EQ(status.memory_cache_misses, 1u);
  EXPECT_EQ(status.memory_cache_hits, 1u);
  EXPECT_EQ(status.failures, 0u);
}

// 显式 EngineState 必须能在非主线程独立加载并渲染
TEST(CodeHighlighterTest, ExplicitEngineRendersOnWorkerThread) {
  QString html;
  bool failed = true;
  std::thread worker([&html, &failed] {
    HighlightEngineState engine;
    const MarkdownDiagnostics diagnostics;
    html = RenderHighlightOnEngine(engine, QStringLiteral("int a = 1;"),
                                   QStringLiteral("cpp"), diagnostics, &failed);
  });
  worker.join();

  EXPECT_FALSE(failed);
  EXPECT_TRUE(html.contains(QStringLiteral("<span"))) << html.toStdString();
}

// ClearMemoryCaches 必须把高亮缓存一起清掉
TEST(CodeHighlighterTest, ClearMemoryCachesClearsHighlightCache) {
  MarkdownRenderContext context;
  const QString code = QStringLiteral("int a = 1;");
  HighlightCode(context, code, QStringLiteral("cpp"));
  context.ClearMemoryCaches();
  HighlightCode(context, code, QStringLiteral("cpp"));

  const MarkdownContextStatus status = context.status().highlight_context;
  EXPECT_EQ(status.renders, 2u);
  EXPECT_EQ(status.memory_cache_hits, 0u);
}

}  // namespace
