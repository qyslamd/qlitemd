#include <gtest/gtest.h>

#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QStandardPaths>
#include <QWidget>

#include "markdown_test_wait.h"
#include "markdown_view.h"

// 重排去抖的判据：整篇 litehtml 布局的次数，而不是 documentHeight（同宽
// 不同高时后者本来就不变，会假阳性）。qlitehtml 的 layoutCount/documentHeight
// 是 Q_INVOKABLE，测试通过 markdownHtml 控件反射调用
int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QStandardPaths::setTestModeEnabled(true);
  QCoreApplication::setOrganizationName(QStringLiteral("md-tests"));
  QCoreApplication::setApplicationName(
      QStringLiteral("layout-debounce-%1")
          .arg(QDateTime::currentMSecsSinceEpoch()));
  QApplication app(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  const int result = RUN_ALL_TESTS();
  QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation) +
       QStringLiteral("/markdown-render"))
      .removeRecursively();
  return result;
}

namespace {

QString LongDoc() {
  QString text = QStringLiteral("# 标题\n\n");
  for (int i = 0; i < 40; ++i) {
    text += QStringLiteral(
                "段落 %1：这是一段用来把文档撑得足够长的正文，"
                "确保纵向滚动条始终存在，避免范围变化干扰布局计数。\n\n")
                .arg(i);
  }
  return text;
}

QWidget* HtmlWidget(MarkdownView& view) {
  return view.findChild<QWidget*>(QStringLiteral("markdownHtml"));
}

int InvokeInt(QWidget* widget, const char* method) {
  int value = -1;
  QMetaObject::invokeMethod(widget, method, Q_RETURN_ARG(int, value));
  return value;
}

int LayoutCount(MarkdownView& view) {
  QWidget* html = HtmlWidget(view);
  return html == nullptr ? -1 : InvokeInt(html, "layoutCount");
}

int DocumentHeight(MarkdownView& view) {
  QWidget* html = HtmlWidget(view);
  return html == nullptr ? -1 : InvokeInt(html, "documentHeight");
}

void PumpMs(int ms) {
  QElapsedTimer clock;
  clock.start();
  while (clock.elapsed() < ms) {
    QApplication::processEvents(QEventLoop::AllEvents, 5);
  }
}

// 每次用新文档重建布局，返回重建后的计数
int SetDocument(MarkdownView& view, const QString& markdown) {
  RenderWaiter waiter(&view);
  view.SetContent(markdown, QString());
  waiter.Wait();
  PumpMs(20);
  return LayoutCount(view);
}

}  // namespace

TEST(LayoutDebounceTest, HeightOnlyResizeKeepsLayout) {
  MarkdownView view;
  view.resize(400, 600);
  view.show();
  view.SetPersistentRenderCacheEnabled(false);
  const int base = SetDocument(view, LongDoc());
  const int height = DocumentHeight(view);
  ASSERT_GT(base, 0);

  view.resize(400, 700);
  PumpMs(50);

  EXPECT_EQ(LayoutCount(view), base) << "纯高度变化不该重排";
  EXPECT_EQ(DocumentHeight(view), height) << "布局没变，文档高度也不该变";
}

TEST(LayoutDebounceTest, WidthResizeRelayouts) {
  MarkdownView view;
  view.resize(400, 600);
  view.show();
  view.SetPersistentRenderCacheEnabled(false);
  const int base = SetDocument(view, LongDoc());
  const int narrow_height = DocumentHeight(view);

  view.resize(800, 600);
  PumpMs(50);

  EXPECT_EQ(LayoutCount(view), base + 1);
  EXPECT_NE(DocumentHeight(view), narrow_height)
      << "变宽后文字重排，高度应变化";
}

TEST(LayoutDebounceTest, ViewportDependentDocumentsAlwaysRelayout) {
  MarkdownView view;
  view.resize(500, 600);
  view.show();
  view.SetPersistentRenderCacheEnabled(false);

  const QStringList guarded = {
      QStringLiteral("<div style=\"height:50%\">x</div>"),
      QStringLiteral("<div style=\"height:var(--h);--h:50%\">x</div>"),
      QStringLiteral("<div style=\"position:relative;top:10%\">x</div>"),
      QStringLiteral("<div style=\"position:fixed;top:0\">x</div>"),
      QStringLiteral("<div style=\"height:/*x*/50%\">x</div>"),
  };
  for (const QString& markdown : guarded) {
    const int base = SetDocument(view, markdown);
    view.resize(500, 650);
    PumpMs(50);
    EXPECT_EQ(LayoutCount(view), base + 1)
        << "守卫漏检：" << markdown.toStdString();
    view.resize(500, 600);
    PumpMs(50);
  }

  // 换回普通文档后守卫必须复位（每次 setHtml 重算，不粘滞）
  const int base = SetDocument(view, LongDoc());
  view.resize(500, 650);
  PumpMs(50);
  EXPECT_EQ(LayoutCount(view), base) << "守卫标志粘滞了";
}

TEST(LayoutDebounceTest, CodeSamplesDoNotDisableTheFastPath) {
  MarkdownView view;
  view.resize(500, 600);
  view.show();
  view.SetPersistentRenderCacheEnabled(false);

  const QString fenced = QStringLiteral(
      "讲解 CSS：\n\n```css\ndiv { height: 100%; position: fixed; }\n```\n");
  const int fenced_base = SetDocument(view, fenced);
  view.resize(500, 650);
  PumpMs(50);
  EXPECT_EQ(LayoutCount(view), fenced_base) << "围栏代码被当成真声明";

  const QString inline_code =
      QStringLiteral("行内代码 `height:100%` 与 `position:fixed`。");
  const int inline_base = SetDocument(view, inline_code);
  view.resize(500, 600);
  PumpMs(50);
  EXPECT_EQ(LayoutCount(view), inline_base) << "行内代码被当成真声明";

  // 行号模式走 <table class="md-code">，同样要剥掉
  view.SetCodeLineNumbers(true);
  PumpMs(50);
  const int numbered_base = SetDocument(view, fenced);
  view.resize(500, 650);
  PumpMs(50);
  EXPECT_EQ(LayoutCount(view), numbered_base) << "行号表格代码被当成真声明";
}

TEST(LayoutDebounceTest, ConsecutiveZoomIsMerged) {
  MarkdownView view;
  view.resize(500, 600);
  view.show();
  view.SetPersistentRenderCacheEnabled(false);
  const int base = SetDocument(view, LongDoc());

  view.SetZoomFactor(1.1);
  view.SetZoomFactor(1.2);
  view.SetZoomFactor(1.3);
  PumpMs(400);

  EXPECT_EQ(LayoutCount(view), base + 1) << "连续缩放应只排最后一档";
  EXPECT_DOUBLE_EQ(view.zoom(), 1.3);
}

TEST(LayoutDebounceTest, SpacedZoomAppliesEachStep) {
  MarkdownView view;
  view.resize(500, 600);
  view.show();
  view.SetPersistentRenderCacheEnabled(false);
  int count = SetDocument(view, LongDoc());

  view.SetZoomFactor(1.1);
  PumpMs(300);
  EXPECT_EQ(LayoutCount(view), count + 1);

  view.SetZoomFactor(1.2);
  PumpMs(300);
  EXPECT_EQ(LayoutCount(view), count + 2) << "间隔超过窗口的调用应各排一次";
}

TEST(LayoutDebounceTest, PendingZoomCostsNoExtraLayoutOnDocumentSwap) {
  MarkdownView view;
  view.resize(500, 600);
  view.show();
  view.SetPersistentRenderCacheEnabled(false);
  const int base = SetDocument(view, LongDoc());

  view.SetZoomFactor(1.4);
  RenderWaiter waiter(&view);
  view.SetContent(LongDoc() + QStringLiteral("\n\n追加段落。\n"), QString());
  waiter.Wait();
  PumpMs(400);

  EXPECT_EQ(LayoutCount(view), base + 1) << "换文档只该有 setHtml 那一次布局";
  EXPECT_DOUBLE_EQ(view.zoom(), 1.4);
}
