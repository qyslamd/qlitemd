#include "markdown_to_html.h"

#include <gtest/gtest.h>

#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>

#include "markdown_render_tasks.h"

namespace {

// GFM 的表格是这套渲染的硬需求之一：用默认 flags 解析时 md4c 会静默丢掉它，
// 渲染结果里只剩一堆竖线，所以这条必须钉住
TEST(MarkdownToHtmlTest, RendersGfmTable) {
  const QString html =
      MarkdownToHtml(QStringLiteral("| 列 A | 列 B |\n|---|---|\n| 1 | 2 |\n"));
  EXPECT_TRUE(html.contains(QStringLiteral("<table>"))) << html.toStdString();
  EXPECT_TRUE(html.contains(QStringLiteral("<th"))) << html.toStdString();
  EXPECT_TRUE(html.contains(QStringLiteral("<td"))) << html.toStdString();
}

TEST(MarkdownToHtmlTest, RendersHeadingsAndInlineCode) {
  const QString html =
      MarkdownToHtml(QStringLiteral("# 标题\n\n正文 `inline` 结束。\n"));
  // 标题一律带锚点：id 是纯 ASCII 的 h-N，slug 挂 data-md-slug（非 ASCII 的
  // id 会被 litehtml 改写，见 heading_ids.h）
  EXPECT_TRUE(html.contains(
      QStringLiteral("<h1 id=\"h-0\" data-md-slug=\"标题\">标题</h1>")))
      << html.toStdString();
  EXPECT_TRUE(html.contains(QStringLiteral("<code>inline</code>")))
      << html.toStdString();
}

// 标题表要经 sink 交到宿主手上：页内 #跳转 就是拿它把 slug 换成元素 id 的。
// 这条钉的是"MarkdownToHtml 有没有把这条线接上"，规则细节归 heading_ids_test
TEST(MarkdownToHtmlTest, HeadingsReachTheSink) {
  QHash<QString, QString> ids;
  const QString html = MarkdownToHtml(
      QStringLiteral("## 跳转测试\n"), FormulaSink(), MermaidSink(),
      CodeBlockSink(),
      [&ids](const QString& slug, const QString& id) { ids.insert(slug, id); });
  ASSERT_EQ(ids.size(), 1);
  EXPECT_EQ(ids.value(QStringLiteral("跳转测试")), QStringLiteral("h-0"));
  EXPECT_TRUE(html.contains(QStringLiteral("id=\"h-0\"")))
      << html.toStdString();
}

// 围栏代码块要留住语言标记（样式与调试都靠它），代码本身要过一遍高亮器
TEST(MarkdownToHtmlTest, HighlightsFencedCodeBlock) {
  const QString html =
      MarkdownToHtml(QStringLiteral("```cpp\nclass Widget {};\n```\n"));
  EXPECT_TRUE(html.contains(QStringLiteral("class=\"language-cpp\"")))
      << html.toStdString();
  EXPECT_TRUE(html.contains(QStringLiteral("<span class=\"kw\">class</span>")))
      << html.toStdString();
}

TEST(MarkdownToHtmlTest, CodeBlockWithoutLanguageHasNoClass) {
  const QString html = MarkdownToHtml(QStringLiteral("```\nplain\n```\n"));
  EXPECT_TRUE(html.contains(QStringLiteral("<pre><code>")))
      << html.toStdString();
}

// 回归点：代码块里的 "<" 从 md4c 出来是 &lt;，高亮器还原再转义，一进一出
// 之后必须还是 &lt;——漏了任何一半都会让代码里的尖括号变成真标签。
// 引号：highlight.js 对字符串里的 ' 用数字实体 &#x27; 转义，显示等价，所以
// 钉的是它而不是字面 '（&apos; 那条还原规则照旧要认，字面引号不许漏成
// "&apos;" 的样子）
TEST(MarkdownToHtmlTest, CodeBlockKeepsAngleBracketsEscaped) {
  const QString html = MarkdownToHtml(
      QStringLiteral("```cpp\nif (a < b && c > d) { x = 'y'; }\n```\n"));
  EXPECT_TRUE(html.contains(QStringLiteral("&lt;"))) << html.toStdString();
  EXPECT_TRUE(html.contains(QStringLiteral("&amp;&amp;")))
      << html.toStdString();
  EXPECT_FALSE(html.contains(QStringLiteral("a < b"))) << html.toStdString();
  EXPECT_TRUE(html.contains(QStringLiteral("&#x27;y&#x27;")))
      << html.toStdString();
  EXPECT_FALSE(html.contains(QStringLiteral("apos;"))) << html.toStdString();
}

// 原始 HTML 按 GitHub 的惯例放行（文档里常用 <br>、<sub>），qlitehtml 不执行
// 脚本；但裸的 "<" 仍要被转义，别让它把后面的文字吞成标签
TEST(MarkdownToHtmlTest, KeepsRawHtmlButEscapesStrayAngle) {
  const QString html = MarkdownToHtml(QStringLiteral("上行<br>下行 a < b\n"));
  EXPECT_TRUE(html.contains(QStringLiteral("<br>"))) << html.toStdString();
  EXPECT_TRUE(html.contains(QStringLiteral("a &lt; b"))) << html.toStdString();
}

// ---------- 公式（抽取与回填是纯逻辑，假渲染器就能验，不必起
// QuickJS）----------

// 记下回调收到的内容，同时返回一个能一眼认出的标记
struct FormulaProbe {
  QStringList texes;
  QVector<bool> displays;

  FormulaSink Sink() {
    return [this](const QString& tex, bool display) {
      texes << tex;
      displays << display;
      return QStringLiteral("<img data-tex=\"%1\">").arg(tex);
    };
  }
};

TEST(MarkdownToHtmlTest, FormulasGoThroughTheSink) {
  FormulaProbe probe;
  const QString html =
      MarkdownToHtml(QStringLiteral("质能 $E = mc^2$ 收尾\n"), probe.Sink());
  ASSERT_EQ(probe.texes.size(), 1);
  EXPECT_EQ(probe.texes.at(0), QStringLiteral("E = mc^2"));
  EXPECT_FALSE(probe.displays.at(0));
  EXPECT_TRUE(html.contains(QStringLiteral("<img data-tex=\"E = mc^2\">")))
      << html.toStdString();
}

// 单独成行的 $$ 会跨行合并成一段独立公式（tex 里的换行要去掉）
TEST(MarkdownToHtmlTest, DisplayBlockSpansLines) {
  FormulaProbe probe;
  const QString html = MarkdownToHtml(
      QStringLiteral("$$\n\\int_0^1 x\\,dx\n$$\n"), probe.Sink());
  ASSERT_EQ(probe.texes.size(), 1);
  EXPECT_TRUE(probe.displays.at(0));
  EXPECT_EQ(probe.texes.at(0).trimmed(), QStringLiteral("\\int_0^1 x\\,dx"));
  EXPECT_FALSE(html.contains(QStringLiteral("$$"))) << html.toStdString();
}

// 代码里的 $ 是字面量：围栏块、行内代码都不该被当成公式
TEST(MarkdownToHtmlTest, DollarsInCodeAreNotFormulas) {
  FormulaProbe probe;
  const QString html = MarkdownToHtml(
      QStringLiteral(
          "```bash\nawk '{ print $1 }'\n```\n\n行内 `$HOME` 与 $x$\n"),
      probe.Sink());
  ASSERT_EQ(probe.texes.size(), 1) << "只有末尾的 $x$ 该进回调";
  EXPECT_EQ(probe.texes.at(0), QStringLiteral("x"));
  EXPECT_TRUE(html.contains(QStringLiteral("print $1"))) << html.toStdString();
  EXPECT_TRUE(html.contains(QStringLiteral("$HOME"))) << html.toStdString();
}

// 正文里的钱不该被配对；转义的 \$ 是字面量
TEST(MarkdownToHtmlTest, MoneyAndEscapedDollarStayText) {
  FormulaProbe probe;
  const QString html = MarkdownToHtml(
      QStringLiteral("价格在 $5 到 $10 之间，转义 \\$y\\$ 也是字面量\n"),
      probe.Sink());
  EXPECT_TRUE(probe.texes.isEmpty()) << html.toStdString();
  EXPECT_TRUE(html.contains(QStringLiteral("$5 到 $10"))) << html.toStdString();
  EXPECT_FALSE(html.contains(QStringLiteral("data-tex"))) << html.toStdString();
}

// 开标记的转义判断与闭标记同一条规则：数前面连续反斜杠的奇偶。偶数个
// 反斜杠之后的 $ 是开标记（老实现只看紧邻字符，把 \$ 整对吃掉，公式没开）
TEST(MarkdownToHtmlTest, EvenBackslashesKeepDollarAsOpenMarker) {
  FormulaProbe probe;
  const QString html =
      MarkdownToHtml(QStringLiteral("\\\\\\\\$x$ 尾\n"), probe.Sink());
  ASSERT_EQ(probe.texes.size(), 1) << html.toStdString();
  EXPECT_EQ(probe.texes.at(0), QStringLiteral("x"));
}

TEST(MarkdownToHtmlTest, TwoBackslashesStillOpenFormula) {
  FormulaProbe probe;
  const QString html =
      MarkdownToHtml(QStringLiteral("\\\\$x$ 尾\n"), probe.Sink());
  ASSERT_EQ(probe.texes.size(), 1) << html.toStdString();
  EXPECT_EQ(probe.texes.at(0), QStringLiteral("x"));
}

// 奇数个反斜杠：$ 是字面量，不能开公式
TEST(MarkdownToHtmlTest, ThreeBackslashesEscapeDollar) {
  FormulaProbe probe;
  const QString html =
      MarkdownToHtml(QStringLiteral("\\\\\\$x$ 尾\n"), probe.Sink());
  EXPECT_TRUE(probe.texes.isEmpty()) << html.toStdString();
}

// 渲染不出来（回调返回空）或调用方没给回调：原文照旧显示，别吞内容
TEST(MarkdownToHtmlTest, FailedFormulaFallsBackToSource) {
  const FormulaSink failing = [](const QString&, bool) { return QString(); };
  const QString html =
      MarkdownToHtml(QStringLiteral("看 $\\frac{a}{b}$ 这里\n"), failing);
  EXPECT_TRUE(html.contains(QStringLiteral("math-src"))) << html.toStdString();
  EXPECT_TRUE(html.contains(QStringLiteral("\\frac{a}{b}")))
      << html.toStdString();

  const QString untouched = MarkdownToHtml(QStringLiteral("看 $x$ 这里\n"));
  EXPECT_TRUE(untouched.contains(QStringLiteral("$x$")))
      << untouched.toStdString();
}

// 没闭合的块级 $$：按原文吐回去，不能吃掉后面的内容
TEST(MarkdownToHtmlTest, UnclosedDisplayBlockStaysText) {
  FormulaProbe probe;
  const QString html = MarkdownToHtml(
      QStringLiteral("$$\n孤零零一段\n\n后面还有正文\n"), probe.Sink());
  EXPECT_TRUE(probe.texes.isEmpty()) << html.toStdString();
  EXPECT_TRUE(html.contains(QStringLiteral("后面还有正文")))
      << html.toStdString();
}

// 占位符格式与用户字面量撞形：占位符要在整份成品 HTML 上全局换回，固定
// 格式（老实现是 @@mathjax-0@@）会被正文/代码里的同形字面量撞上、错换成
// 公式。现在带每次调用的随机盐，同形概率可忽略——字面量必须原样活着
TEST(MarkdownToHtmlTest, PlaceholderShapedUserTextStaysLiteral) {
  FormulaProbe probe;
  const QString html =
      MarkdownToHtml(QStringLiteral("正文里有字面 @@mathjax-0@@ 和 "
                                    "@@mj-abc123-0@@ 字样，加公式 $x^2$。\n"),
                     probe.Sink());
  ASSERT_EQ(probe.texes.size(), 1);
  EXPECT_EQ(probe.texes.at(0), QStringLiteral("x^2"));
  EXPECT_TRUE(html.contains(QStringLiteral("@@mathjax-0@@")))
      << html.toStdString();
  EXPECT_TRUE(html.contains(QStringLiteral("@@mj-abc123-0@@")))
      << html.toStdString();
}

// ---------- 代码块行号（可选） ----------

MarkdownRenderOptions LineNumberOptions() {
  MarkdownRenderOptions options;
  options.code_line_numbers = true;
  return options;
}

// 默认关闭：结构与不带选项时逐字节一致
TEST(MarkdownToHtmlTest, CodeLineNumbersOffKeepsPreCode) {
  const QString html = MarkdownToHtml(QStringLiteral("```cpp\nint a;\n```\n"));
  EXPECT_TRUE(html.contains(QStringLiteral("<pre><code")))
      << html.toStdString();
  EXPECT_FALSE(html.contains(QStringLiteral("md-code"))) << html.toStdString();
}

TEST(MarkdownToHtmlTest, CodeLineNumbersEmitNumberedRows) {
  const QString html = MarkdownToHtml(
      QStringLiteral("```cpp\nint a = 1;\nint b = 2;\n```\n"), FormulaSink(),
      MermaidSink(), CodeBlockSink(), HeadingSink(), LineNumberOptions());
  EXPECT_TRUE(html.contains(QStringLiteral("<table class=\"md-code\">")))
      << html.toStdString();
  EXPECT_TRUE(html.contains(QStringLiteral("md-code-ln\">1<")))
      << html.toStdString();
  EXPECT_TRUE(html.contains(QStringLiteral("md-code-ln\">2<")))
      << html.toStdString();
  EXPECT_EQ(html.count(QStringLiteral("<tr>")), 2) << html.toStdString();
  // 高亮本身不受影响
  EXPECT_TRUE(html.contains(QStringLiteral("<span class=\"ty\">int</span>")))
      << html.toStdString();
}

// 中间空行也要保留一行高度，否则 tr 会塌
TEST(MarkdownToHtmlTest, CodeLineNumbersKeepBlankRows) {
  const QString html = MarkdownToHtml(
      QStringLiteral("```cpp\nint a;\n\nint b;\n```\n"), FormulaSink(),
      MermaidSink(), CodeBlockSink(), HeadingSink(), LineNumberOptions());
  EXPECT_EQ(html.count(QStringLiteral("<tr>")), 3) << html.toStdString();
  EXPECT_TRUE(html.contains(QStringLiteral("&#160;"))) << html.toStdString();
}

// 空代码块零行；只有一个空行的块是一行
TEST(MarkdownToHtmlTest, CodeLineNumbersEmptyBlockRules) {
  const QString empty =
      MarkdownToHtml(QStringLiteral("```\n```\n"), FormulaSink(), MermaidSink(),
                     CodeBlockSink(), HeadingSink(), LineNumberOptions());
  EXPECT_FALSE(empty.contains(QStringLiteral("md-code-ln")))
      << empty.toStdString();

  const QString one_blank = MarkdownToHtml(
      QStringLiteral("```\n\n```\n"), FormulaSink(), MermaidSink(),
      CodeBlockSink(), HeadingSink(), LineNumberOptions());
  EXPECT_EQ(one_blank.count(QStringLiteral("<tr>")), 1)
      << one_blank.toStdString();
}

// 每个 md-code-line 单元格内的 span 必须配平：载荷逐行闭合的契约不能在
// 拼表格时被打破
TEST(MarkdownToHtmlTest, CodeLineNumbersKeepSpansBalancedPerCell) {
  const QString html = MarkdownToHtml(
      QStringLiteral("```cpp\n/* 第一行\n第二行 */ int a = 1;\n```\n"),
      FormulaSink(), MermaidSink(), CodeBlockSink(), HeadingSink(),
      LineNumberOptions());
  const QString marker = QStringLiteral("<td class=\"md-code-line\">");
  int pos = 0;
  int cells = 0;
  while ((pos = html.indexOf(marker, pos)) >= 0) {
    const int start = pos + marker.size();
    const int end = html.indexOf(QStringLiteral("</td>"), start);
    ASSERT_GE(end, 0);
    const QString cell = html.mid(start, end - start);
    EXPECT_EQ(cell.count(QStringLiteral("<span")),
              cell.count(QStringLiteral("</span>")))
        << cell.toStdString();
    pos = end;
    ++cells;
  }
  EXPECT_EQ(cells, 2) << html.toStdString();
}

// 行号模式不影响复制链路：sink 仍拿原始源码，copy: 链接仍在
TEST(MarkdownToHtmlTest, CodeLineNumbersKeepCopyLink) {
  QHash<QString, QString> blocks;
  const QString html = MarkdownToHtml(
      QStringLiteral("```cpp\nint a;\n```\n"), FormulaSink(), MermaidSink(),
      [&blocks](const QString& id, const QString& code) {
        blocks.insert(id, code);
      },
      HeadingSink(), LineNumberOptions());
  EXPECT_TRUE(html.contains(QStringLiteral("href=\"copy:0\"")))
      << html.toStdString();
  ASSERT_EQ(blocks.size(), 1);
  EXPECT_EQ(blocks.value(QStringLiteral("0")), QStringLiteral("int a;\n"));
}

// 缩进代码块（4 空格，CommonMark）里的 $ 是字面量。块不能打断段落，
// 所以用空行隔开的写法
TEST(MarkdownToHtmlTest, IndentedCodeBlockDollarsStayLiteral) {
  FormulaProbe probe;
  const QString html = MarkdownToHtml(
      QStringLiteral("引子\n\n    cost $5 and $10 total\n\n收尾 $x$ 尾。\n"),
      probe.Sink());
  ASSERT_EQ(probe.texes.size(), 1);
  EXPECT_EQ(probe.texes.at(0), QStringLiteral("x"));
  EXPECT_TRUE(html.contains(QStringLiteral("cost $5 and $10 total")))
      << html.toStdString();
}

// 围栏闭合要同字符、不短于开启、后面只剩空白：```` 开的块不能被 ``` 提前
// 收掉，否则块里的 $ 会被当正文抽走
TEST(MarkdownToHtmlTest, FenceCloseMustBeAtLeastAsLong) {
  FormulaProbe probe;
  const QString html = MarkdownToHtml(
      QStringLiteral("````\n$x$\n```\n$y$\n````\n\n收尾 $z$ 尾。\n"),
      probe.Sink());
  ASSERT_EQ(probe.texes.size(), 1);
  EXPECT_EQ(probe.texes.at(0), QStringLiteral("z"));
  EXPECT_TRUE(html.contains(QStringLiteral("$x$"))) << html.toStdString();
  EXPECT_TRUE(html.contains(QStringLiteral("$y$"))) << html.toStdString();
}

// $$ 的闭合要跳过转义位置：$$a \$$ b$$ 里第一个 $$ 是转义内容，
// 直接 indexOf 会把公式截成 "a \"、后半截漏到正文
TEST(MarkdownToHtmlTest, DisplayCloseSkipsEscapedDollars) {
  FormulaProbe probe;
  const QString html =
      MarkdownToHtml(QStringLiteral("$$a \\$$ b$$ 后文\n"), probe.Sink());
  ASSERT_EQ(probe.texes.size(), 1);
  EXPECT_EQ(probe.texes.at(0), QStringLiteral("a \\$$ b"));
  EXPECT_TRUE(html.contains(QStringLiteral("后文"))) << html.toStdString();
}

// 段落开着的缩进行只是续行（CommonMark），里面的 $ 照常当公式——
// 缩进代码块的判定不能过宽
TEST(MarkdownToHtmlTest, IndentedContinuationStillExtractsFormulas) {
  FormulaProbe probe;
  const QString html = MarkdownToHtml(
      QStringLiteral("段落首行\n    续行 $y$ 收\n"), probe.Sink());
  ASSERT_EQ(probe.texes.size(), 1);
  EXPECT_EQ(probe.texes.at(0), QStringLiteral("y"));
}

// 缩进代码块里的 ``` 是内容（CommonMark），不能当围栏开关——否则它会
// 翻转围栏态，后面的正文 $ 被错吞成"围栏内容"
TEST(MarkdownToHtmlTest, IndentedFenceLinesAreCodeContent) {
  FormulaProbe probe;
  const QString html = MarkdownToHtml(
      QStringLiteral("引子\n\n    ```py\n    $x$\n    ```\n\n收尾 $y$ 尾。\n"),
      probe.Sink());
  ASSERT_EQ(probe.texes.size(), 1);
  EXPECT_EQ(probe.texes.at(0), QStringLiteral("y"));
  EXPECT_TRUE(html.contains(QStringLiteral("$x$"))) << html.toStdString();
}

// 块级标记之后不能再把缩进行当段落续行：标题、分隔线、Setext 下划线、
// 空引用都是完整块，后面的 4 空格行应进缩进代码块，块里的 $ 是字面量
TEST(MarkdownToHtmlTest, HeadingEndsParagraphBeforeIndentedCode) {
  FormulaProbe probe;
  const QString html = MarkdownToHtml(
      QStringLiteral("# Title\n    code $x$\n\n尾 $y$ 尾。\n"), probe.Sink());
  ASSERT_EQ(probe.texes.size(), 1) << html.toStdString();
  EXPECT_EQ(probe.texes.at(0), QStringLiteral("y"));
  EXPECT_TRUE(html.contains(QStringLiteral("$x$"))) << html.toStdString();
}

TEST(MarkdownToHtmlTest, ThematicBreakEndsParagraphBeforeIndentedCode) {
  FormulaProbe probe;
  const QString html = MarkdownToHtml(
      QStringLiteral("引子\n\n---\n    code $x$\n\n尾 $y$ 尾。\n"),
      probe.Sink());
  ASSERT_EQ(probe.texes.size(), 1) << html.toStdString();
  EXPECT_EQ(probe.texes.at(0), QStringLiteral("y"));
  EXPECT_TRUE(html.contains(QStringLiteral("$x$"))) << html.toStdString();
}

TEST(MarkdownToHtmlTest, SetextUnderlineEndsParagraphBeforeIndentedCode) {
  FormulaProbe probe;
  const QString html = MarkdownToHtml(
      QStringLiteral("标题\n===\n    code $x$\n\n尾 $y$ 尾。\n"), probe.Sink());
  ASSERT_EQ(probe.texes.size(), 1) << html.toStdString();
  EXPECT_EQ(probe.texes.at(0), QStringLiteral("y"));
  EXPECT_TRUE(html.contains(QStringLiteral("$x$"))) << html.toStdString();
}

// 反向保护：引用行有正文时，后面的缩进行是 CommonMark 的 lazy
// continuation（pandoc 实测），不能被当成代码块
TEST(MarkdownToHtmlTest, BlockQuoteKeepsLazyContinuation) {
  FormulaProbe probe;
  const QString html =
      MarkdownToHtml(QStringLiteral("> quote\n    code $x$\n"), probe.Sink());
  ASSERT_EQ(probe.texes.size(), 1) << html.toStdString();
  EXPECT_EQ(probe.texes.at(0), QStringLiteral("x"));
}

TEST(MarkdownToHtmlTest, EmptyBlockQuoteEndsParagraphBeforeIndentedCode) {
  FormulaProbe probe;
  const QString html = MarkdownToHtml(
      QStringLiteral(">\n    code $x$\n\n尾 $y$ 尾。\n"), probe.Sink());
  ASSERT_EQ(probe.texes.size(), 1) << html.toStdString();
  EXPECT_EQ(probe.texes.at(0), QStringLiteral("y"));
}

// 空行后的 "=====" 只是普通正文（CommonMark），不能当 Setext 下划线：
// 后面的缩进行仍是段落续行
TEST(MarkdownToHtmlTest, StandaloneEqualsLineIsParagraph) {
  FormulaProbe probe;
  const QString html =
      MarkdownToHtml(QStringLiteral("=====\n    code $x$\n"), probe.Sink());
  ASSERT_EQ(probe.texes.size(), 1) << html.toStdString();
  EXPECT_EQ(probe.texes.at(0), QStringLiteral("x"));
}

// 列表项的缩进是内容续行，不是缩进代码块
TEST(MarkdownToHtmlTest, ListContinuationIsNotCode) {
  FormulaProbe probe;
  const QString html = MarkdownToHtml(
      QStringLiteral("- item\n    continuation $x$\n"), probe.Sink());
  ASSERT_EQ(probe.texes.size(), 1) << html.toStdString();
  EXPECT_EQ(probe.texes.at(0), QStringLiteral("x"));
}

// 缩进代码块结束后紧跟围栏：结束那行不能吞掉围栏判定（老实现里围栏
// 判定先走，非缩进的 ``` 会被正确认出；这条钉的是重构后的行为不变）
TEST(MarkdownToHtmlTest, IndentedCodeThenFenceStillWorks) {
  FormulaProbe probe;
  const QString html = MarkdownToHtml(
      QStringLiteral("引子\n\n    indented\n```py\n$x$\n```\n收尾 $y$ 尾。\n"),
      probe.Sink());
  ASSERT_EQ(probe.texes.size(), 1);
  EXPECT_EQ(probe.texes.at(0), QStringLiteral("y"));
}

// 回归点：md4c 在 GFM 下放行内联原始 HTML，正文里没闭合的裸 <pre><code> 若不加
// 行首锚点，会一直匹配到文档后面下一个 </code></pre>，把中间整段正文吞成代码
TEST(MarkdownToHtmlTest, StrayInlinePreCodeDoesNotSwallowLaterBlocks) {
  const QString html = MarkdownToHtml(
      QStringLiteral("text <pre><code> tail\n\n```cpp\nint a;\n```\n"));
  // 后面那个真围栏块照常成块且带上语言类；highlight.js 把裸类型归为
  // type（.ty）而不是 keyword——比旧手写分词器拆得细，这里顺带钉住
  EXPECT_TRUE(html.contains(QStringLiteral("class=\"language-cpp\"")))
      << html.toStdString();
  EXPECT_TRUE(html.contains(QStringLiteral("<span class=\"ty\">int</span>")))
      << html.toStdString();
  // 正文那截没被当成代码吞掉
  EXPECT_FALSE(html.contains(QStringLiteral("tail&lt;/p&gt;")))
      << html.toStdString();
}

// 代码块里的实体往返：md4c 转义一次、高亮器还原再转义一次，字面量不能被二次解码
TEST(MarkdownToHtmlTest, CodeBlockEntitiesRoundTrip) {
  const QString html =
      MarkdownToHtml(QStringLiteral("```\na &amp;lt; b\nA & B\n```\n"));
  EXPECT_TRUE(html.contains(QStringLiteral("&amp;amp;lt;")))
      << html.toStdString();
  EXPECT_TRUE(html.contains(QStringLiteral("A &amp; B"))) << html.toStdString();
}

// 未闭合围栏由 md4c 自己补齐；空输入不该产出半截标签
TEST(MarkdownToHtmlTest, UnclosedFenceAndEmptyInput) {
  const QString html = MarkdownToHtml(QStringLiteral("```cpp\nint a;\n"));
  EXPECT_TRUE(html.contains(QStringLiteral("</code></pre>")))
      << html.toStdString();

  const QString empty = MarkdownToHtml(QString());
  EXPECT_FALSE(empty.contains(QStringLiteral("<pre>"))) << empty.toStdString();
}

// 复制按钮：每个代码块包一层带 header 的框，header 里有语言标签与 copy: 链接；
// 回调按 id 收到的是**未高亮的原始源码**（复制出去的是源码，不是 HTML），
// 且逐字照给——含结尾那个换行，不做 trim（复制走的就是文档里写的样子）
TEST(MarkdownToHtmlTest, CodeBlockGetsCopyButtonAndHeader) {
  QHash<QString, QString> blocks;
  const QString html = MarkdownToHtml(
      QStringLiteral("```cpp\nint a = 1;  // 甲\n```\n"), FormulaSink(),
      MermaidSink(), [&blocks](const QString& id, const QString& code) {
        blocks.insert(id, code);
      });

  EXPECT_TRUE(html.contains(QStringLiteral("<div class=\"code-wrap\">")))
      << html.toStdString();
  EXPECT_TRUE(html.contains(QStringLiteral("<span class=\"code-lang\">cpp")))
      << html.toStdString();
  ASSERT_EQ(blocks.size(), 1);
  const QString id = blocks.constBegin().key();
  EXPECT_TRUE(html.contains(QStringLiteral("href=\"copy:%1\"").arg(id)))
      << html.toStdString();
  EXPECT_EQ(blocks.value(id), QStringLiteral("int a = 1;  // 甲\n"));
}

// 多个代码块各自一个 id，按出现顺序编号——查表串了会复制出别的块的代码
TEST(MarkdownToHtmlTest, CodeBlockIdsAreUniqueAndOrdered) {
  QHash<QString, QString> blocks;
  const QString html = MarkdownToHtml(
      QStringLiteral("```cpp\n甲\n```\n\n```python\n乙\n```\n"), FormulaSink(),
      MermaidSink(), [&blocks](const QString& id, const QString& code) {
        blocks.insert(id, code);
      });

  ASSERT_EQ(blocks.size(), 2);
  EXPECT_EQ(blocks.value(QStringLiteral("0")), QStringLiteral("甲\n"));
  EXPECT_EQ(blocks.value(QStringLiteral("1")), QStringLiteral("乙\n"));
  EXPECT_TRUE(html.contains(QStringLiteral("href=\"copy:0\"")))
      << html.toStdString();
  EXPECT_TRUE(html.contains(QStringLiteral("href=\"copy:1\"")))
      << html.toStdString();
}

// 无语言的围栏：header 显示 text，但 <code> 上不许凭空写 language- 类
// （那是"声明了哪种语言"的契约）
TEST(MarkdownToHtmlTest, LanguageLessBlockKeepsCodeClassClean) {
  const QString html =
      MarkdownToHtml(QStringLiteral("```\nplain\n```\n"), FormulaSink(),
                     MermaidSink(), [](const QString&, const QString&) {});
  EXPECT_TRUE(html.contains(QStringLiteral("<span class=\"code-lang\">text")))
      << html.toStdString();
  EXPECT_TRUE(html.contains(QStringLiteral("<pre><code>")))
      << html.toStdString();
}

// 不传 code_blocks 时也要照常出复制按钮（宿主没接表，点了没反应而已）——
// 渲染层不能因为宿主没接就改变产出的结构
TEST(MarkdownToHtmlTest, CopyButtonAppearsWithoutSink) {
  const QString html = MarkdownToHtml(QStringLiteral("```cpp\nint a;\n```\n"));
  EXPECT_TRUE(html.contains(QStringLiteral("href=\"copy:0\"")))
      << html.toStdString();
}

// Parse/Assemble 拆开后，用同一组假渲染结果组装，产物必须与同步参考实现
// 逐字节一致
TEST(MarkdownToHtmlTest, ParseAndAssembleMatchSyncOutput) {
  const QString markdown = QStringLiteral(
      "# 标题\n\n公式 $x+1$ 与 $x+1$\n\n```cpp\nint a;\n```\n\n"
      "```mermaid\nflowchart LR\n  A --> B\n```\n");
  const MarkdownRenderResolver resolver =
      [](MarkdownRenderKind kind, const QString& payload,
         const QString& language, bool display) {
        switch (kind) {
          case MarkdownRenderKind::Formula:
            return QStringLiteral("<img data-tex=\"%1\" data-display=\"%2\">")
                .arg(payload)
                .arg(display);
          case MarkdownRenderKind::Mermaid:
            return QStringLiteral("<div class=\"mermaid-mock\">%1</div>")
                .arg(payload);
          case MarkdownRenderKind::Highlight:
            return QStringLiteral("<span class=\"mock-%1\">%2</span>")
                .arg(language, payload);
        }
        return QString();
      };
  const MarkdownParseResult parsed = ParseMarkdown(markdown, true);
  ASSERT_FALSE(parsed.parse_error);
  const QString assembled =
      AssembleMarkdown(parsed, resolver, CodeBlockSink(), HeadingSink(), {});

  MarkdownRenderOptions options;
  options.highlight = [](const QString& code, const QString& language) {
    return QStringLiteral("<span class=\"mock-%1\">%2</span>")
        .arg(language, code);
  };
  const QString reference = MarkdownToHtml(
      markdown,
      [](const QString& tex, bool display) {
        return QStringLiteral("<img data-tex=\"%1\" data-display=\"%2\">")
            .arg(tex)
            .arg(display);
      },
      [](const QString& code) {
        return QStringLiteral("<div class=\"mermaid-mock\">%1</div>").arg(code);
      },
      CodeBlockSink(), HeadingSink(), options);

  EXPECT_EQ(assembled, reference);
}

// 关闭公式时盐也必须存在；否则 RestoreMath 会把用户正文里的字面占位符
// 当成公式槽吞掉
TEST(MarkdownToHtmlTest, LiteralPlaceholderSurvivesWithoutFormulaSink) {
  const QString html = MarkdownToHtml(QStringLiteral("@@mj--0@@\n"));
  EXPECT_TRUE(html.contains(QStringLiteral("@@mj--0@@"))) << html.toStdString();
}

// 高亮 resolver 返回空（取消/缺失）时，组装层必须退回纯转义，不能出空代码块
TEST(MarkdownToHtmlTest, AssembleFallsBackWhenHighlightIsEmpty) {
  const MarkdownParseResult parsed =
      ParseMarkdown(QStringLiteral("```cpp\na < b;\n```\n"), false);
  ASSERT_FALSE(parsed.parse_error);
  const QString html =
      AssembleMarkdown(parsed,
                       [](MarkdownRenderKind, const QString&, const QString&,
                          bool) { return QString(); },
                       CodeBlockSink(), HeadingSink(), {});
  EXPECT_TRUE(html.contains(QStringLiteral("a &lt; b;"))) << html.toStdString();
}

// 任务收集：公式同 key 去重；每个代码块有高亮任务；mermaid 额外有图任务，
// 且高亮任务保留作失败回退
TEST(MarkdownToHtmlTest, CollectTasksDedupesAndKeepsMermaidFallback) {
  const QString markdown = QStringLiteral(
      "$x$\n\n$x$\n\n```cpp\nint a;\n```\n\n"
      "```mermaid\nflowchart LR\n  A --> B\n```\n");
  const MarkdownParseResult parsed = ParseMarkdown(markdown, true);
  ASSERT_FALSE(parsed.parse_error);
  const QVector<MarkdownRenderTask> tasks =
      CollectRenderTasks(parsed, true, QColor(), MarkdownTheme::Light);

  int formula = 0;
  int highlight = 0;
  int mermaid = 0;
  for (const MarkdownRenderTask& task : tasks) {
    if (task.kind == MarkdownRenderKind::Formula) {
      ++formula;
      EXPECT_EQ(task.indices.size(), 2);
    } else if (task.kind == MarkdownRenderKind::Highlight) {
      ++highlight;
    } else if (task.kind == MarkdownRenderKind::Mermaid) {
      ++mermaid;
      EXPECT_EQ(task.indices.size(), 1);
    }
  }
  EXPECT_EQ(formula, 1);
  EXPECT_EQ(highlight, 2);
  EXPECT_EQ(mermaid, 1);
}

}  // namespace
