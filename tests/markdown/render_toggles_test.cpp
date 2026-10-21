#include <gtest/gtest.h>

#include <QApplication>
#include <QBuffer>
#include <QDateTime>
#include <QDir>
#include <QImage>
#include <QMetaObject>
#include <QPixmap>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QUrl>
#include <QWidget>

#include "markdown_render_context.h"
#include "markdown_test_wait.h"
#include "markdown_view.h"

// 开关的行为判据。用真正的 MarkdownView + QLiteHtmlWidget（offscreen），
// 通过 Q_INVOKABLE html() 读回渲染后的 HTML；不依赖控件内部类型。
//
// 执行顺序有意义：gtest 按定义顺序跑，"关闭时不加载引擎"的用例必须排在
// 任何会真正渲染公式/Mermaid 的用例之前（本进程的引擎是共享的）。
int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  // LoadDefaultCss 只在首次 ApplyDocument 读该变量；外部残留会让主题断言
  // 看错 CSS
  qunsetenv("MD_MARKDOWN_CSS");
  // 持久缓存默认开启：测试模式 + 每次运行唯一应用名，避免读到上一次留下
  // 的条目（否则 renders 计数会变成 0）
  QStandardPaths::setTestModeEnabled(true);
  QCoreApplication::setOrganizationName(QStringLiteral("md-tests"));
  QCoreApplication::setApplicationName(
      QStringLiteral("toggle-%1").arg(QDateTime::currentMSecsSinceEpoch()));
  QApplication app(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  const int result = RUN_ALL_TESTS();
  QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation) +
       QStringLiteral("/markdown-render"))
      .removeRecursively();
  return result;
}

namespace {

QString HtmlOf(MarkdownView& view) {
  QWidget* html = view.findChild<QWidget*>(QStringLiteral("markdownHtml"));
  if (html == nullptr) {
    return {};
  }
  QString text;
  QMetaObject::invokeMethod(html, "html", Q_RETURN_ARG(QString, text));
  return text;
}

TEST(RenderTogglesTest, DisabledRenderersDoNotLoadOrRender) {
  MarkdownView view;
  view.SetFormulaRenderingEnabled(false);
  view.SetMermaidRenderingEnabled(false);
  RenderWaiter wait(&view);
  view.SetContent(
      QStringLiteral("价格 $x^2$ 收尾\n\n"
                     "```mermaid\nflowchart LR\n    A --> B\n```\n"),
      QString());
  ASSERT_TRUE(wait.Wait());

  const QString html = HtmlOf(view);
  ASSERT_FALSE(html.isEmpty());
  EXPECT_TRUE(html.contains(QStringLiteral("$x^2$"))) << html.toStdString();
  EXPECT_FALSE(html.contains(QStringLiteral("data:image/png;base64")))
      << html.toStdString();
  EXPECT_FALSE(html.contains(QStringLiteral("mj:f/"))) << html.toStdString();
  // mermaid 围栏退回普通代码块
  EXPECT_TRUE(html.contains(QStringLiteral("<pre><code")))
      << html.toStdString();

  const MarkdownRendererStatus status = view.renderer_status();
  EXPECT_FALSE(status.formula.loaded);
  EXPECT_FALSE(status.mermaid.loaded);
}

TEST(RenderTogglesTest, EnablingFormulaRendersOnReapply) {
  MarkdownView view;
  view.SetFormulaRenderingEnabled(false);
  RenderWaiter first(&view);
  view.SetContent(QStringLiteral("$x^2$\n"), QString());
  ASSERT_TRUE(first.Wait());
  EXPECT_FALSE(view.renderer_status().formula.loaded);

  RenderWaiter reapply(&view);
  view.SetFormulaRenderingEnabled(true);
  ASSERT_TRUE(reapply.Wait());
  const QString html = HtmlOf(view);
  EXPECT_TRUE(html.contains(QStringLiteral("mj:f/"))) << html.toStdString();
  EXPECT_FALSE(html.contains(QStringLiteral("data:image/png;base64")))
      << html.toStdString();
  ASSERT_TRUE(view.renderer_status().formula.loaded);

  // 再 ApplyDocument 一次：公式命中内容缓存、图片命中 token 缓存，
  // 不能重新渲染，也不能出现 token 悬空
  const MarkdownRendererStatus before = view.renderer_status();
  RenderWaiter line_numbers(&view);
  view.SetCodeLineNumbers(true);
  ASSERT_TRUE(line_numbers.Wait());
  const QString reapply_html = HtmlOf(view);
  EXPECT_TRUE(reapply_html.contains(QStringLiteral("mj:f/")))
      << reapply_html.toStdString();
  const MarkdownRendererStatus after = view.renderer_status();
  EXPECT_EQ(after.formula_context.renders, before.formula_context.renders);
  EXPECT_GT(after.image_context.image_cache_hits,
            before.image_context.image_cache_hits);
}

TEST(ImageTokenTest, TokenIsStableAndUrlSafe) {
  const QString token =
      MakeImageToken(QStringLiteral("f/"), QStringLiteral("D#ff0000|x^2"));
  EXPECT_TRUE(token.startsWith(QStringLiteral("mj:f/")));
  EXPECT_EQ(token.size(), QStringLiteral("mj:f/").size() + 64);
  const QUrl url(token);
  EXPECT_EQ(url.scheme(), QStringLiteral("mj"));
  EXPECT_TRUE(url.host().isEmpty());
  EXPECT_EQ(QString::fromLatin1(url.toEncoded()), token);
  EXPECT_EQ(QUrl::fromEncoded(token.toUtf8()), url);
  EXPECT_EQ(token, MakeImageToken(QStringLiteral("f/"),
                                  QStringLiteral("D#ff0000|x^2")));
  EXPECT_NE(token,
            MakeImageToken(QStringLiteral("f/"), QStringLiteral("D#ff0000|y")));
}

TEST(ImageTokenTest, TokenizesPngAndPreservesAttributes) {
  QImage image(2, 2, QImage::Format_ARGB32_Premultiplied);
  image.fill(Qt::red);
  QByteArray png;
  QBuffer buffer(&png);
  ASSERT_TRUE(buffer.open(QIODevice::WriteOnly));
  ASSERT_TRUE(image.save(&buffer, "PNG"));
  const QString tag =
      QStringLiteral(
          "<img class=\"mermaid\" src=\"data:image/png;base64,%1\" "
          "width=\"2\" height=\"2\">")
          .arg(QString::fromLatin1(png.toBase64()));

  QPixmap pixmap;
  const QString token = QStringLiteral("mj:m/abc");
  const QString out = TokenizeImageTag(tag, token, &pixmap);
  EXPECT_FALSE(pixmap.isNull());
  EXPECT_FALSE(out.contains(QStringLiteral("data:image/png")));
  EXPECT_TRUE(out.contains(QStringLiteral("src=\"mj:m/abc\"")));
  EXPECT_TRUE(out.contains(QStringLiteral("class=\"mermaid\"")));
  EXPECT_TRUE(out.contains(QStringLiteral("width=\"2\"")));
  EXPECT_TRUE(out.contains(QStringLiteral("height=\"2\"")));
}

TEST(ImageTokenTest, InvalidTagsFallBackUnchanged) {
  QPixmap pixmap;
  EXPECT_TRUE(
      TokenizeImageTag(QString(), QStringLiteral("mj:f/x"), &pixmap).isEmpty());
  EXPECT_TRUE(pixmap.isNull());

  const QString corrupt = QStringLiteral(
      "<img src=\"data:image/png;base64,AAAA\" "
      "width=\"2\" height=\"2\">");
  EXPECT_EQ(TokenizeImageTag(corrupt, QStringLiteral("mj:f/x"), &pixmap),
            corrupt);
  EXPECT_TRUE(pixmap.isNull());

  const QString not_png = QStringLiteral(
      "<img src=\"file:///tmp/a.png\" width=\"2\" height=\"2\">");
  EXPECT_EQ(TokenizeImageTag(not_png, QStringLiteral("mj:f/x"), &pixmap),
            not_png);
  EXPECT_TRUE(pixmap.isNull());
}

TEST(RenderTogglesTest, LineNumbersToggleChangesStructure) {
  MarkdownView view;
  RenderWaiter first(&view);
  view.SetContent(QStringLiteral("```cpp\nint a;\n```\n"), QString());
  ASSERT_TRUE(first.Wait());
  ASSERT_TRUE(HtmlOf(view).contains(QStringLiteral("<pre><code")));

  RenderWaiter line_numbers_on(&view);
  view.SetCodeLineNumbers(true);
  ASSERT_TRUE(line_numbers_on.Wait());
  QString html = HtmlOf(view);
  EXPECT_TRUE(html.contains(QStringLiteral("<table class=\"md-code\">")))
      << html.toStdString();
  EXPECT_TRUE(html.contains(QStringLiteral("md-code-ln")))
      << html.toStdString();

  RenderWaiter line_numbers_off(&view);
  view.SetCodeLineNumbers(false);
  ASSERT_TRUE(line_numbers_off.Wait());
  html = HtmlOf(view);
  EXPECT_TRUE(html.contains(QStringLiteral("<pre><code")))
      << html.toStdString();
  // 注意不能断言不含 "md-code"：整份文档里带着 CSS 规则
  EXPECT_FALSE(html.contains(QStringLiteral("<table class=\"md-code\">")))
      << html.toStdString();
}

TEST(RenderTogglesTest, StatusCountsRenders) {
  MarkdownView view;
  view.ResetRendererStats();
  RenderWaiter wait(&view);
  view.SetContent(QStringLiteral("```cpp\nint a;\n```\n"), QString());
  ASSERT_TRUE(wait.Wait());
  const MarkdownRendererStatus status = view.renderer_status();
  EXPECT_EQ(status.highlight_context.renders, 1U);
}

// 主题切换 = 换样式表 + 重渲染（公式颜色变了，缓存 key 也变）
TEST(RenderTogglesTest, ThemeSwitchUpdatesCssAndRerenders) {
  MarkdownView view;
  RenderWaiter first(&view);
  view.SetContent(QStringLiteral("$x$\n"), QString());
  ASSERT_TRUE(first.Wait());
  EXPECT_EQ(view.theme(), MarkdownTheme::Light);
  const QString light_html = HtmlOf(view);
  ASSERT_FALSE(light_html.isEmpty());
  const quint64 renders = view.renderer_status().formula_context.renders;
  ASSERT_GE(renders, 1U);

  RenderWaiter theme_wait(&view);
  view.SetTheme(MarkdownTheme::Dark);
  ASSERT_TRUE(theme_wait.Wait());
  EXPECT_EQ(view.theme(), MarkdownTheme::Dark);
  const QString dark_html = HtmlOf(view);
  EXPECT_NE(light_html, dark_html);
  EXPECT_TRUE(dark_html.contains(QStringLiteral("#0d1117")))
      << dark_html.toStdString();
  // 新颜色是新的缓存 key，必须真的再渲染一次
  EXPECT_EQ(view.renderer_status().formula_context.renders, renders + 1);
}

// 大纲与文档坐标：outline 按出现顺序与层级给出，AnchorY 能按 slug 取 y；
// 找不到返回 nullopt，蒙混不成"在顶部"
TEST(RenderTogglesTest, OutlineAndAnchorYFollowDocument) {
  MarkdownView view;
  view.resize(800, 600);
  view.show();
  QApplication::processEvents();

  int changed = 0;
  QObject::connect(&view, &MarkdownView::outlineChanged,
                   [&changed] { ++changed; });
  RenderWaiter wait(&view);
  view.SetContent(QStringLiteral("# A\n\n第一段\n\n## B\n\n第二段\n"),
                  QString());
  ASSERT_TRUE(wait.Wait());

  EXPECT_EQ(changed, 1);
  ASSERT_EQ(view.outline().size(), 2);
  EXPECT_EQ(view.outline().at(0).level, 1);
  EXPECT_EQ(view.outline().at(0).text, QStringLiteral("A"));
  EXPECT_EQ(view.outline().at(0).anchor, QStringLiteral("a"));
  EXPECT_EQ(view.outline().at(1).anchor, QStringLiteral("b"));

  const std::optional<int> a = view.AnchorY(QStringLiteral("a"));
  const std::optional<int> b = view.AnchorY(QStringLiteral("b"));
  ASSERT_TRUE(a.has_value()) << "顶部标题也该有文档坐标";
  ASSERT_TRUE(b.has_value());
  EXPECT_GE(*a, 0);
  EXPECT_GT(*b, *a);
  EXPECT_FALSE(view.AnchorY(QStringLiteral("missing")).has_value());
}

// 重渲染后大纲会被宿主重建、选中清空；滚动位置没变时 valueChanged 不发，
// 必须显式补报一次坐标，宿主才能恢复当前章节
TEST(RenderTogglesTest, ReapplyReportsScrollPosition) {
  MarkdownView view;
  view.resize(800, 600);
  view.show();
  RenderWaiter first(&view);
  view.SetContent(QStringLiteral("# A\n\n正文\n"), QString());
  ASSERT_TRUE(first.Wait());

  int reports = 0;
  int last_y = -1;
  QObject::connect(&view, &MarkdownView::viewportScrolled,
                   [&reports, &last_y](int y) {
                     ++reports;
                     last_y = y;
                   });
  RenderWaiter theme_wait(&view);
  view.SetTheme(MarkdownTheme::Dark);
  ASSERT_TRUE(theme_wait.Wait());

  EXPECT_GE(reports, 1) << "重渲染后必须补报滚动位置";
  EXPECT_EQ(last_y, 0) << "文档在顶部，坐标应为 0";
}

// 状态计数要覆盖 Mermaid，而不只是高亮/公式
TEST(RenderTogglesTest, StatusCountsMermaidRenders) {
  QTemporaryDir dir;
  ASSERT_TRUE(dir.isValid());
  MarkdownView view;
  view.SetRenderCacheDirectory(dir.path());
  RenderWaiter wait(&view);
  view.SetContent(
      QStringLiteral("```mermaid\nflowchart LR\n    A --> B\n```\n"),
      QString());
  ASSERT_TRUE(wait.Wait());
  const MarkdownRendererStatus status = view.renderer_status();
  EXPECT_TRUE(status.mermaid.loaded);
  EXPECT_EQ(status.mermaid_context.renders, 1U);
  EXPECT_EQ(status.mermaid_context.failures, 0U);
}

// 持久缓存开关与目录：默认开；指向临时目录后渲染应落盘；清目录只清模块
// 子目录；关闭后不再写盘
TEST(RenderTogglesTest, PersistentCacheCanBeConfiguredAndCleared) {
  QTemporaryDir dir;
  ASSERT_TRUE(dir.isValid());
  MarkdownView view;
  EXPECT_TRUE(view.persistent_render_cache_enabled());

  view.SetRenderCacheDirectory(dir.path());
  EXPECT_EQ(view.render_context().options().cache_directory, dir.path());
  RenderWaiter first(&view);
  view.SetContent(QStringLiteral("$x$\n"), QString());
  ASSERT_TRUE(first.Wait());
  const QString formula_dir =
      QDir(dir.path()).filePath(QStringLiteral("formula"));
  EXPECT_FALSE(QDir(formula_dir).entryList(QDir::Files).isEmpty())
      << "开启持久缓存后应落盘";

  view.ClearPersistentRenderCache();
  EXPECT_TRUE(QDir(formula_dir).entryList(QDir::Files).isEmpty());

  view.SetPersistentRenderCacheEnabled(false);
  EXPECT_FALSE(view.persistent_render_cache_enabled());
  RenderWaiter second(&view);
  view.SetContent(QStringLiteral("$y$\n"), QString());
  ASSERT_TRUE(second.Wait());
  EXPECT_FALSE(QDir(formula_dir).exists()) << "关闭后不应再写盘";
}

}  // namespace
