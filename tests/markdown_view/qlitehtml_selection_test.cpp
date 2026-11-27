// 选择边界的回归：空行不是独立元素，而是块之间的 margin。光标落在
// margin/容器上时，不能把整个 .md-page/body 当成"整个元素已选中"。
#include <QApplication>
#include <QMouseEvent>
#include <QPoint>
#include <QTest>

#include <gtest/gtest.h>

#include "qlitehtmlwidget.h"

class TestLiteHtmlWidget final : public QLiteHtmlWidget {
 public:
  using QLiteHtmlWidget::mouseDoubleClickEvent;
  using QLiteHtmlWidget::mouseMoveEvent;
  using QLiteHtmlWidget::mousePressEvent;
  using QLiteHtmlWidget::mouseReleaseEvent;
};

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  qputenv("QT_SCALE_FACTOR", "1");
  QApplication app(argc, argv);
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

namespace {

// Layout is deterministic: line-height 30, paragraph margin-bottom 70.
// AAA: y 10..40, BBB: y 110..140, CCC: y 210..240.
constexpr char kHtml[] =
    "<html><head><style>"
    "html,body{margin:0;padding:0}"
    "#page{padding:10px;width:320px;font-size:20px;line-height:30px}"
    "p{margin:0 0 70px 0}"
    "p:last-child{margin-bottom:0}"
    "</style></head><body><div id='page'>"
    "<p>AAA</p><p>BBB</p><p>CCC</p><p>DDD</p>"
    "</div></body></html>";

QPoint ToParent(TestLiteHtmlWidget* view, const QPoint& viewport_pos) {
  return view->viewport()->mapToParent(viewport_pos);
}

void SendMouse(TestLiteHtmlWidget* view, QEvent::Type type, const QPoint& pos,
               Qt::MouseButton button, Qt::MouseButtons buttons) {
  QMouseEvent event(type, ToParent(view, pos), button, buttons, Qt::NoModifier);
  switch (type) {
    case QEvent::MouseButtonPress:
      view->mousePressEvent(&event);
      break;
    case QEvent::MouseMove:
      view->mouseMoveEvent(&event);
      break;
    case QEvent::MouseButtonRelease:
      view->mouseReleaseEvent(&event);
      break;
    case QEvent::MouseButtonDblClick:
      view->mouseDoubleClickEvent(&event);
      break;
    default:
      break;
  }
}

void Drag(TestLiteHtmlWidget* view, const QPoint& from, const QPoint& to) {
  SendMouse(view, QEvent::MouseButtonPress, from, Qt::LeftButton,
            Qt::LeftButton);
  for (int step = 1; step <= 4; ++step) {
    const QPoint point = from + (to - from) * step / 4;
    SendMouse(view, QEvent::MouseMove, point, Qt::NoButton, Qt::LeftButton);
  }
  SendMouse(view, QEvent::MouseButtonRelease, to, Qt::LeftButton, Qt::NoButton);
  QTest::qWait(20);
}

void ShowHtml(TestLiteHtmlWidget* view, const char* html) {
  view->resize(400, 400);
  view->setHtml(QString::fromLatin1(html));
  view->show();
  QTest::qWait(100);
}

void ShowSelectionDocument(TestLiteHtmlWidget* view) { ShowHtml(view, kHtml); }

QString Selected(TestLiteHtmlWidget* view) { return view->selectedText(); }

constexpr char kQuoteHtml[] =
    "<html><head><style>"
    "html,body{margin:0;padding:0}"
    "#page{padding:10px;width:320px;font-size:20px;line-height:30px}"
    "blockquote{margin:0;padding:10px 10px 10px 20px}"
    "blockquote p{margin:0 0 60px 0}"
    "blockquote p:last-child{margin-bottom:0}"
    "</style></head><body><div id='page'>"
    "<blockquote><p>QQQ</p><p>RRR</p></blockquote>"
    "</div></body></html>";

}  // namespace

TEST(QliteHtmlSelectionTest, NormalTextToTextSelectionIsUnchanged) {
  TestLiteHtmlWidget view;
  ShowSelectionDocument(&view);
  Drag(&view, QPoint(12, 20), QPoint(100, 230));
  const QString text = Selected(&view);
  EXPECT_TRUE(text.contains(QStringLiteral("AAA"))) << text.toStdString();
  EXPECT_TRUE(text.contains(QStringLiteral("BBB"))) << text.toStdString();
  EXPECT_TRUE(text.contains(QStringLiteral("CCC"))) << text.toStdString();
  EXPECT_FALSE(text.contains(QStringLiteral("DDD"))) << text.toStdString();
}

TEST(QliteHtmlSelectionTest, EndingInBlankLineDoesNotSelectWholeDocument) {
  TestLiteHtmlWidget view;
  ShowSelectionDocument(&view);
  // 60 is closer to AAA (bottom 40) than to BBB (top 110).
  Drag(&view, QPoint(12, 20), QPoint(12, 60));
  const QString text = Selected(&view);
  EXPECT_TRUE(text.contains(QStringLiteral("AAA"))) << text.toStdString();
  EXPECT_FALSE(text.contains(QStringLiteral("BBB"))) << text.toStdString();
  EXPECT_FALSE(text.contains(QStringLiteral("CCC"))) << text.toStdString();
}

TEST(QliteHtmlSelectionTest, StartingInBlankLineClampsToNearestLine) {
  TestLiteHtmlWidget view;
  ShowSelectionDocument(&view);
  // 90 is closer to BBB (top 110) than to AAA (bottom 40).
  Drag(&view, QPoint(12, 90), QPoint(100, 130));
  const QString text = Selected(&view);
  EXPECT_TRUE(text.contains(QStringLiteral("BBB"))) << text.toStdString();
  EXPECT_FALSE(text.contains(QStringLiteral("AAA"))) << text.toStdString();
  EXPECT_FALSE(text.contains(QStringLiteral("CCC"))) << text.toStdString();
}

TEST(QliteHtmlSelectionTest, BlankToBlankDoesNotSelectWholeDocument) {
  TestLiteHtmlWidget view;
  ShowSelectionDocument(&view);
  Drag(&view, QPoint(12, 60), QPoint(12, 90));
  const QString text = Selected(&view);
  EXPECT_TRUE(text.trimmed().isEmpty()) << text.toStdString();
}

TEST(QliteHtmlSelectionTest, PagePaddingClampsToNearestLine) {
  TestLiteHtmlWidget view;
  ShowSelectionDocument(&view);
  Drag(&view, QPoint(2, 20), QPoint(100, 130));
  const QString text = Selected(&view);
  EXPECT_TRUE(text.contains(QStringLiteral("AAA"))) << text.toStdString();
  EXPECT_TRUE(text.contains(QStringLiteral("BBB"))) << text.toStdString();
  EXPECT_FALSE(text.contains(QStringLiteral("CCC"))) << text.toStdString();
}

TEST(QliteHtmlSelectionTest, DoubleClickDoesNotChangeSubsequentDragMode) {
  TestLiteHtmlWidget view;
  ShowSelectionDocument(&view);
  SendMouse(&view, QEvent::MouseButtonDblClick, QPoint(12, 20), Qt::LeftButton,
            Qt::LeftButton);
  QTest::qWait(20);
  Drag(&view, QPoint(12, 20), QPoint(12, 60));
  const QString text = Selected(&view);
  EXPECT_TRUE(text.contains(QStringLiteral("AAA"))) << text.toStdString();
  EXPECT_FALSE(text.contains(QStringLiteral("BBB"))) << text.toStdString();
  EXPECT_FALSE(text.contains(QStringLiteral("CCC"))) << text.toStdString();
}

TEST(QliteHtmlSelectionTest,
     SelectingLastLineToBottomDoesNotSelectWholeDocument) {
  TestLiteHtmlWidget view;
  ShowSelectionDocument(&view);
  Drag(&view, QPoint(12, 320), QPoint(100, 390));
  const QString text = Selected(&view);
  EXPECT_TRUE(text.contains(QStringLiteral("DDD"))) << text.toStdString();
  EXPECT_FALSE(text.contains(QStringLiteral("AAA"))) << text.toStdString();
  EXPECT_FALSE(text.contains(QStringLiteral("BBB"))) << text.toStdString();
  EXPECT_FALSE(text.contains(QStringLiteral("CCC"))) << text.toStdString();
}

TEST(QliteHtmlSelectionTest, NestedContainerGapClampsToItsOwnText) {
  TestLiteHtmlWidget view;
  ShowHtml(&view, kQuoteHtml);
  // The gap between the two quote paragraphs belongs to the blockquote.
  Drag(&view, QPoint(30, 70), QPoint(100, 120));
  const QString text = Selected(&view);
  EXPECT_TRUE(text.contains(QStringLiteral("RRR"))) << text.toStdString();
  EXPECT_FALSE(text.contains(QStringLiteral("QQQ"))) << text.toStdString();
}
