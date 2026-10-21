// 视图的两个"能力透传"：查找与打印。两者都是 QLiteHtmlWidget 的既有能力，
// 本模块只加了守卫（空串、空指针）与语义约定，所以判据取行为的最小面——
// 找得到/找不到、PDF 真的出得来。
//
// 无头跑：offscreen 平台不建真窗口；QPrinter(PdfFormat) 也不需要真打印机，
// 直接往临时文件写。这里不验"排出来长什么样"——像素验收需要真实界面环境
#include <QApplication>
#include <QByteArray>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QPrinter>
#include <QStandardPaths>
#include <QString>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include "formula_renderer.h"
#include "markdown_test_wait.h"
#include "markdown_theme.h"
#include "markdown_view.h"

int main(int argc, char** argv) {
  // 只在无显示的 Linux 上切 offscreen。Windows 留着默认平台——打印用例
  // 需要打印后端，offscreen 没有（那种环境下用例自己 skip）
#ifdef Q_OS_LINUX
  if (qEnvironmentVariableIsEmpty("DISPLAY") &&
      qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY")) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
  }
#endif
  // 视图默认开启持久缓存：测试模式 + 唯一应用名，别读写真实缓存目录
  QStandardPaths::setTestModeEnabled(true);
  QCoreApplication::setOrganizationName(QStringLiteral("md-tests"));
  QCoreApplication::setApplicationName(
      QStringLiteral("view-actions-%1")
          .arg(QDateTime::currentMSecsSinceEpoch()));
  QApplication app(argc, argv);
  testing::InitGoogleTest(&argc, argv);
  const int result = RUN_ALL_TESTS();
  QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation) +
       QStringLiteral("/markdown-render"))
      .removeRecursively();
  return result;
}

namespace {

// 一篇够"长"的文档：打印用例要有内容可分页，查找用例要有靶子
QString SampleMarkdown() {
  return QStringLiteral(
      "# 标题\n\n正文里藏着 needle 这个词，还有别的字。\n\n"
      "## 第二节\n\n再来一段撑开版面，打印时至少能看出一页。\n");
}

bool SetContentAndWait(MarkdownView& view, const QString& markdown,
                       const QString& base_dir) {
  RenderWaiter waiter(&view);
  view.SetContent(markdown, base_dir);
  return waiter.Wait();
}

TEST(MarkdownViewActionsTest, FindHits) {
  MarkdownView view;
  ASSERT_TRUE(SetContentAndWait(view, SampleMarkdown(), QString()));
  bool wrapped = true;  // 先置反，确认实现真的写了它
  EXPECT_TRUE(view.FindText(QStringLiteral("needle"), MarkdownView::FindFlags(),
                            &wrapped));
  EXPECT_FALSE(wrapped);
}

TEST(MarkdownViewActionsTest, FindMissesWithoutTouchingSelection) {
  MarkdownView view;
  ASSERT_TRUE(SetContentAndWait(view, SampleMarkdown(), QString()));
  EXPECT_FALSE(
      view.FindText(QStringLiteral("完全没有的词"), MarkdownView::FindFlags()));
}

// 空串是误用，不是"找全部"：必须原样返回 false，不滚不选
TEST(MarkdownViewActionsTest, EmptyNeedleIsRejected) {
  MarkdownView view;
  ASSERT_TRUE(SetContentAndWait(view, SampleMarkdown(), QString()));
  EXPECT_FALSE(view.FindText(QString(), MarkdownView::FindFlags()));
}

TEST(MarkdownViewActionsTest, PrintWritesPdf) {
  MarkdownView view;
  ASSERT_TRUE(SetContentAndWait(view, SampleMarkdown(), QString()));

  QTemporaryDir dir;
  ASSERT_TRUE(dir.isValid());
  const QString path = dir.filePath(QStringLiteral("out.pdf"));

  QPrinter printer(QPrinter::HighResolution);
  printer.setOutputFormat(QPrinter::PdfFormat);
  printer.setOutputFileName(path);
  if (!printer.isValid()) {
    GTEST_SKIP() << "本平台没有打印后端（如 offscreen），跳过 PDF 用例";
  }
  view.Print(&printer);

  QFile pdf(path);
  ASSERT_TRUE(pdf.open(QIODevice::ReadOnly))
      << "没写出 PDF：" << path.toStdString();
  EXPECT_EQ(pdf.read(5), QByteArray("%PDF-"));
  EXPECT_GT(pdf.size(), 1000);  // 空文档也会有个骨架，太小说明什么都没画
}

// 打印容器是新建的 DocumentContainer，只复制了 dataCallback 时 token 图会
// 全部空白；PDF 里必须真的出现图片对象
TEST(MarkdownViewActionsTest, PrintIncludesFormulaImage) {
  MarkdownView view;
  ASSERT_TRUE(SetContentAndWait(view, QStringLiteral("$x^2$\n"), QString()));

  QTemporaryDir dir;
  ASSERT_TRUE(dir.isValid());
  const QString path = dir.filePath(QStringLiteral("formula.pdf"));

  QPrinter printer(QPrinter::HighResolution);
  printer.setOutputFormat(QPrinter::PdfFormat);
  printer.setOutputFileName(path);
  if (!printer.isValid()) {
    GTEST_SKIP() << "本平台没有打印后端（如 offscreen），跳过 PDF 用例";
  }
  view.Print(&printer);

  QFile pdf(path);
  ASSERT_TRUE(pdf.open(QIODevice::ReadOnly));
  const QByteArray data = pdf.readAll();
  EXPECT_TRUE(data.contains("/Subtype /Image") ||
              data.contains("/Subtype/Image"))
      << data.left(2000).toStdString();
}

namespace {

int PdfPageCount(const QByteArray& pdf) {
  int count = 0;
  int from = 0;
  while ((from = pdf.indexOf("/Type /Page", from)) >= 0) {
    // 排除 /Type /Pages
    if (pdf.at(from + 11) != 's') {
      ++count;
    }
    from += 11;
  }
  return count;
}

}  // namespace

// 回归：HighResolution 打印机下必须按 CSS 逻辑像素布局。曾经把设备像素
// （约 9921 px 宽）当逻辑像素传给 litehtml，文字被缩到几十分之一，且分页
// 循环拿设备页高比逻辑文档高，永远只有一页
TEST(MarkdownViewActionsTest, PrintHighResolutionPaginatesLongDocument) {
  QString markdown;
  for (int i = 0; i < 200; ++i) {
    markdown += QStringLiteral(
                    "第 %1 段：这是一段用来撑开打印页数的正文，"
                    "每段都足够长，确保文档跨越多个 A4 页面。\n\n")
                    .arg(i);
  }
  MarkdownView view;
  ASSERT_TRUE(SetContentAndWait(view, markdown, QString()));

  QTemporaryDir dir;
  ASSERT_TRUE(dir.isValid());
  const QString path = dir.filePath(QStringLiteral("long.pdf"));
  QPrinter printer(QPrinter::HighResolution);
  printer.setOutputFormat(QPrinter::PdfFormat);
  printer.setOutputFileName(path);
  if (!printer.isValid()) {
    GTEST_SKIP() << "本平台没有打印后端（如 offscreen），跳过 PDF 用例";
  }
  view.Print(&printer);

  QFile pdf(path);
  ASSERT_TRUE(pdf.open(QIODevice::ReadOnly));
  const QByteArray bytes = pdf.readAll();
  EXPECT_GE(PdfPageCount(bytes), 2)
      << "长文档只出了一页：页面/文档高度单位可能又错位了";
}

// 空指针不崩：宿主忘了判空时也别炸
TEST(MarkdownViewActionsTest, PrintNullPrinterIsNoop) {
  MarkdownView view;
  ASSERT_TRUE(SetContentAndWait(view, SampleMarkdown(), QString()));
  view.Print(nullptr);
}

TEST(MarkdownViewActionsTest, ClearSelectionClearsFindHighlight) {
  MarkdownView view;
  ASSERT_TRUE(SetContentAndWait(view, SampleMarkdown(), QString()));
  ASSERT_TRUE(
      view.FindText(QStringLiteral("needle"), MarkdownView::FindFlags()));
  EXPECT_TRUE(view.has_selection());
  view.ClearSelection();
  EXPECT_FALSE(view.has_selection());
  EXPECT_TRUE(
      view.FindText(QStringLiteral("needle"), MarkdownView::FindFlags()));
}

TEST(MarkdownViewActionsTest, ImageInfoMapsFormulaToken) {
  MarkdownView view;
  ASSERT_TRUE(SetContentAndWait(view, QStringLiteral("$x^2$\n"), QString()));
  const QString key = FormulaRenderKey(QStringLiteral("x^2"), false,
                                       ThemeFormulaColor(view.theme()));
  const QUrl token(MakeImageToken(QStringLiteral("f/"), key));
  const std::optional<MarkdownImageInfo> info = view.ImageInfo(token);
  ASSERT_TRUE(info.has_value());
  EXPECT_EQ(info->kind, MarkdownImageKind::kFormula);
  EXPECT_EQ(info->source, QStringLiteral("x^2"));
  EXPECT_FALSE(info->display);
  EXPECT_FALSE(view.ImagePixmap(token).isNull());
}

TEST(MarkdownViewActionsTest, ExportHtmlCopiesImagesAndRewritesTokens) {
  QTemporaryDir dir;
  ASSERT_TRUE(dir.isValid());
  const QString doc_dir = dir.filePath(QStringLiteral("中文 空格&amp"));
  ASSERT_TRUE(QDir().mkpath(doc_dir));
  QImage image(2, 2, QImage::Format_ARGB32);
  image.fill(Qt::red);
  ASSERT_TRUE(image.save(doc_dir + QStringLiteral("/图 1.png")));

  MarkdownView view;
  ASSERT_TRUE(SetContentAndWait(
      view, QStringLiteral("$x$\n\n![alt](<图 1.png>)\n"), doc_dir));
  const QString html_path = dir.filePath(QStringLiteral("out & 文件.html"));
  QString error;
  ASSERT_TRUE(view.ExportHtml(html_path, &error)) << error.toStdString();

  QFile html_file(html_path);
  ASSERT_TRUE(html_file.open(QIODevice::ReadOnly));
  const QString html = QString::fromUtf8(html_file.readAll());
  EXPECT_TRUE(
      html.contains(QStringLiteral("out%20%26%20%E6%96%87%E4%BB%B6_files/")));
  EXPECT_FALSE(html.contains(QStringLiteral("mj:")));

  const QDir assets(dir.filePath(QStringLiteral("out & 文件_files")));
  ASSERT_TRUE(assets.exists());
  EXPECT_GE(assets.entryList(QDir::Files).size(), 2);
}

// 取消待提交的新文档后，后续 Reapply 必须回到屏幕上那份已提交文档
TEST(MarkdownViewActionsTest, CancelPendingRenderRollsBackCommittedDoc) {
  MarkdownView view;
  ASSERT_TRUE(
      SetContentAndWait(view, QStringLiteral("# A\n\n旧文档\n"), QString()));
  QSignalSpy progress(&view, &MarkdownView::renderProgress);
  view.SetContent(QStringLiteral("$b$\n\n# B\n\n新文档\n"), QString());
  QTRY_VERIFY_WITH_TIMEOUT(progress.count() > 0, 60000);
  view.CancelPendingRender();

  RenderWaiter waiter(&view);
  view.SetTheme(MarkdownTheme::Dark);
  ASSERT_TRUE(waiter.Wait());
  EXPECT_TRUE(
      view.FindText(QStringLiteral("旧文档"), MarkdownView::FindFlags()));
  EXPECT_FALSE(
      view.FindText(QStringLiteral("新文档"), MarkdownView::FindFlags()));
}

// 占位页只是临时的：提交会覆盖它，取消则回到屏幕上那份已提交文档
TEST(MarkdownViewActionsTest, PlaceholderIsReplacedOrRolledBack) {
  MarkdownView view;
  ASSERT_TRUE(
      SetContentAndWait(view, QStringLiteral("# A\n\n旧文档\n"), QString()));
  view.ShowPlaceholder(QStringLiteral("正在渲染…"));
  EXPECT_TRUE(
      view.FindText(QStringLiteral("正在渲染"), MarkdownView::FindFlags()));

  view.CancelPendingRender();
  EXPECT_TRUE(
      view.FindText(QStringLiteral("旧文档"), MarkdownView::FindFlags()));
  EXPECT_FALSE(
      view.FindText(QStringLiteral("正在渲染"), MarkdownView::FindFlags()));

  view.ShowPlaceholder(QStringLiteral("正在渲染…"));
  ASSERT_TRUE(
      SetContentAndWait(view, QStringLiteral("# B\n\n新文档\n"), QString()));
  EXPECT_TRUE(
      view.FindText(QStringLiteral("新文档"), MarkdownView::FindFlags()));
  EXPECT_FALSE(
      view.FindText(QStringLiteral("正在渲染"), MarkdownView::FindFlags()));
}

// 前一代还没渲染完就换文档：只允许后一代提交
TEST(MarkdownViewActionsTest, StaleRequestDoesNotCommit) {
  MarkdownView view;
  QSignalSpy progress(&view, &MarkdownView::renderProgress);
  view.SetContent(QStringLiteral("$a+b$\n\n# 旧\n"), QString());
  QTRY_VERIFY_WITH_TIMEOUT(progress.count() > 0, 60000);

  QSignalSpy finished(&view, &MarkdownView::renderFinished);
  view.SetContent(QStringLiteral("# 新\n\nneedle\n"), QString());
  ASSERT_TRUE(finished.wait(60000));
  EXPECT_TRUE(
      view.FindText(QStringLiteral("needle"), MarkdownView::FindFlags()));
  EXPECT_FALSE(view.FindText(QStringLiteral("旧"), MarkdownView::FindFlags()));
}

// 进度与收尾都带同一个 request_id；有公式时 total 应为 1
TEST(MarkdownViewActionsTest, EmitsProgressThenFinished) {
  MarkdownView view;
  QSignalSpy finished(&view, &MarkdownView::renderFinished);
  QSignalSpy progress(&view, &MarkdownView::renderProgress);
  view.SetContent(QStringLiteral("$x$\n"), QString());

  ASSERT_TRUE(finished.wait(60000));
  ASSERT_GT(progress.count(), 0);
  EXPECT_EQ(progress.last().at(0).toULongLong(),
            finished.last().at(0).toULongLong());
  EXPECT_GE(progress.last().at(2).toInt(), 1);
}

// 文本直载也算"有文档"：SetContent 只落 markdown_、不动 current_file_，
// has_document 只查后者的话这里恒为 false（md_viewer 的导出守卫就踩过）
TEST(MarkdownViewActionsTest, SetContentMarksDocumentAsPresent) {
  MarkdownView view;
  EXPECT_FALSE(view.has_document());
  ASSERT_TRUE(SetContentAndWait(view, SampleMarkdown(), QString()));
  EXPECT_TRUE(view.has_document());
  EXPECT_TRUE(view.current_file().isEmpty());  // 依旧没有文件归属
}

}  // namespace
