// 选择边界的回归：空行不是独立元素，而是块之间的 margin。光标落在
// margin/容器上时，不能把整个 .md-page/body 当成"整个元素已选中"。
#include <QApplication>
#include <QColor>
#include <QImage>
#include <QMouseEvent>
#include <QPoint>
#include <QScrollBar>
#include <QStyle>
#include <QStyleOptionSlider>
#include <QTest>
#include <QTextDocument>

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

TEST(QliteHtmlSelectionTest, TextUsesIBeamCursorAndGapUsesArrow) {
  TestLiteHtmlWidget view;
  ShowSelectionDocument(&view);
  SendMouse(&view, QEvent::MouseMove, QPoint(12, 20), Qt::NoButton,
            Qt::NoButton);
  EXPECT_EQ(view.viewport()->cursor().shape(), Qt::IBeamCursor);
  SendMouse(&view, QEvent::MouseMove, QPoint(12, 60), Qt::NoButton,
            Qt::NoButton);
  EXPECT_EQ(view.viewport()->cursor().shape(), Qt::ArrowCursor);
}

constexpr char kSearchHtml[] =
    "<html><head><style>"
    "html,body{margin:0;padding:0;background:#fff;color:#000}"
    "#page{padding:10px;width:320px;font-size:20px;line-height:40px}"
    "</style></head><body><div id='page'>alpha beta gamma</div>"
    "</body></html>";

TEST(QliteHtmlSelectionTest, SearchMatchUsesItsOwnHighlightColor) {
  TestLiteHtmlWidget view;
  ShowHtml(&view, kSearchHtml);
  const QColor amber(255, 196, 0, 72);
  view.setSearchHighlightColor(amber);
  ASSERT_TRUE(
      view.findText(QStringLiteral("beta"), QTextDocument::FindFlags(), false));
  EXPECT_TRUE(view.isSearchSelection());
  EXPECT_EQ(view.searchHighlightColor(), amber);

  const QImage image = view.grab().toImage();
  int amber_pixels = 0;
  for (int y = 0; y < image.height(); ++y) {
    for (int x = 0; x < image.width(); ++x) {
      const QColor pixel = image.pixelColor(x, y);
      if (qAbs(pixel.red() - 255) <= 3 && qAbs(pixel.green() - 238) <= 4 &&
          qAbs(pixel.blue() - 183) <= 4) {
        ++amber_pixels;
      }
    }
  }
  EXPECT_GT(amber_pixels, 20) << "search highlight is not the configured amber";

  SendMouse(&view, QEvent::MouseButtonPress, QPoint(12, 20), Qt::LeftButton,
            Qt::LeftButton);
  SendMouse(&view, QEvent::MouseButtonRelease, QPoint(12, 20), Qt::LeftButton,
            Qt::NoButton);
  EXPECT_FALSE(view.isSearchSelection());
  const QImage after_click = view.grab().toImage();
  int amber_after_click = 0;
  for (int y = 0; y < after_click.height(); ++y) {
    for (int x = 0; x < after_click.width(); ++x) {
      const QColor pixel = after_click.pixelColor(x, y);
      if (qAbs(pixel.red() - 255) <= 3 && qAbs(pixel.green() - 238) <= 4 &&
          qAbs(pixel.blue() - 183) <= 4) {
        ++amber_after_click;
      }
    }
  }
  EXPECT_GT(amber_after_click, 20)
      << "search highlight must survive a click into the document";
}

constexpr char kMultiSearchHtml[] =
    "<html><head><style>"
    "html,body{margin:0;padding:0;background:#fff;color:#000}"
    "#page{padding:10px;width:320px;font-size:20px;line-height:30px}"
    "p{margin:0 0 10px 0}"
    "</style></head><body><div id='page'>"
    "<p>alpha one</p><p>alpha two</p><p>alpha three</p>"
    "</div></body></html>";

constexpr char kLongSearchHtml[] =
    "<html><head><style>"
    "html,body{margin:0;padding:0;background:#fff;color:#000}"
    "#page{padding:10px;width:320px;font-size:20px;line-height:30px}"
    "p{margin:0 0 20px 0}"
    "</style></head><body><div id='page'>"
    "<p>needle 0</p><p>needle 1</p><p>needle 2</p><p>needle 3</p>"
    "<p>needle 4</p><p>needle 5</p><p>needle 6</p><p>needle 7</p>"
    "<p>needle 8</p><p>needle 9</p><p>needle 10</p><p>needle 11</p>"
    "</div></body></html>";

QWidget* FindSearchOverlay(TestLiteHtmlWidget* view) {
  return view->verticalScrollBar()->findChild<QWidget*>(
      QStringLiteral("searchMarkOverlay"));
}

QRect ScrollBarGroove(QScrollBar* bar) {
  QStyleOptionSlider option;
  option.initFrom(bar);
  option.orientation = bar->orientation();
  QRect groove = bar->style()->subControlRect(QStyle::CC_ScrollBar, &option,
                                              QStyle::SC_ScrollBarGroove, bar);
  if (!groove.isValid() || groove.height() <= 0)
    groove = bar->rect();
  return groove;
}

TEST(QliteHtmlSelectionTest, SearchMarksAppearAndClickJumpsToMatch) {
  TestLiteHtmlWidget view;
  ShowHtml(&view, kLongSearchHtml);
  QScrollBar* bar = view.verticalScrollBar();
  ASSERT_GT(bar->maximum(), 0) << "test document must be scrollable";

  int index = -1;
  int count = 0;
  bool wrapped = false;
  ASSERT_TRUE(view.findText(QStringLiteral("needle"),
                            QTextDocument::FindFlags(), false, &wrapped, &index,
                            &count));
  ASSERT_EQ(count, 12);
  ASSERT_EQ(view.searchMatchRects().size(), count);

  QWidget* overlay = FindSearchOverlay(&view);
  ASSERT_NE(overlay, nullptr);
  EXPECT_TRUE(overlay->isVisible());
  EXPECT_EQ(overlay->geometry(), bar->rect());
  const QImage marks_image = overlay->grab().toImage();
  int mark_pixels = 0;
  for (int y = 0; y < marks_image.height(); ++y) {
    for (int x = 0; x < marks_image.width(); ++x) {
      if (qAlpha(marks_image.pixel(x, y)) > 0)
        ++mark_pixels;
    }
  }
  EXPECT_GT(mark_pixels, 0) << "search marks were not painted";

  const int target = 6;
  const int document_height = view.documentHeight();
  ASSERT_GT(document_height, 0);
  const int match_top = view.searchMatchRects()[target].top();
  const QRect groove = ScrollBarGroove(bar);
  const int y = groove.top() + qRound(qreal(match_top) / document_height *
                                      (groove.height() - 1));
  const QPoint click_pos(bar->width() - 4, y);
  // The overlay must not steal scrollbar input.
  EXPECT_EQ(bar->childAt(click_pos), nullptr);
  QMouseEvent press(QEvent::MouseButtonPress, click_pos,
                    bar->mapToGlobal(click_pos), Qt::LeftButton, Qt::LeftButton,
                    Qt::NoModifier);
  QApplication::sendEvent(bar, &press);
  EXPECT_EQ(view.currentSearchMatch(), target);
  EXPECT_EQ(view.selectedText(), QStringLiteral("needle"));

  // Clicking into the document drops the text selection and focus, but the
  // find session (count, current match and scrollbar marks) must survive.
  SendMouse(&view, QEvent::MouseButtonPress, QPoint(20, 120), Qt::LeftButton,
            Qt::LeftButton);
  SendMouse(&view, QEvent::MouseButtonRelease, QPoint(20, 120), Qt::LeftButton,
            Qt::NoButton);
  EXPECT_FALSE(view.isSearchSelection());
  EXPECT_EQ(view.currentSearchMatch(), target);
  EXPECT_EQ(view.searchMatches().size(), count);
  EXPECT_TRUE(overlay->isVisible());
  ASSERT_TRUE(view.findText(QStringLiteral("needle"),
                            QTextDocument::FindFlags(), false, &wrapped, &index,
                            &count));
  EXPECT_EQ(index, target + 1);

  view.clearSelection();
  EXPECT_FALSE(view.isSearchSelection());
  EXPECT_FALSE(overlay->isVisible());
}

TEST(QliteHtmlSelectionTest, NoMatchClearsPreviousSearchSelectionAndMarks) {
  TestLiteHtmlWidget view;
  ShowHtml(&view, kLongSearchHtml);
  QWidget* overlay = nullptr;
  int index = -1;
  int count = 0;
  bool wrapped = false;
  ASSERT_TRUE(view.findText(QStringLiteral("needle"),
                            QTextDocument::FindFlags(), false, &wrapped, &index,
                            &count));
  ASSERT_EQ(count, 12);
  overlay = FindSearchOverlay(&view);
  ASSERT_NE(overlay, nullptr);
  ASSERT_TRUE(overlay->isVisible());

  EXPECT_FALSE(view.findText(QStringLiteral("absent-query"),
                             QTextDocument::FindFlags(), false, &wrapped,
                             &index, &count));
  EXPECT_EQ(index, -1);
  EXPECT_EQ(count, 0);
  EXPECT_TRUE(view.selectedText().isEmpty());
  EXPECT_FALSE(view.isSearchSelection());
  EXPECT_FALSE(overlay->isVisible());
}

TEST(QliteHtmlSelectionTest, SearchReportsCountAndNavigatesMatches) {
  TestLiteHtmlWidget view;
  ShowHtml(&view, kMultiSearchHtml);
  int index = -1;
  int count = 0;
  bool wrapped = false;
  ASSERT_TRUE(view.findText(QStringLiteral("alpha"), QTextDocument::FindFlags(),
                            false, &wrapped, &index, &count));
  EXPECT_EQ(index, 0);
  EXPECT_EQ(count, 3);
  EXPECT_EQ(view.currentSearchMatch(), 0);
  EXPECT_EQ(view.searchMatches().size(), 3);
  EXPECT_EQ(view.searchMatchRects().size(), 3);
  for (const QRect& rect : view.searchMatchRects())
    EXPECT_FALSE(rect.isEmpty());
  EXPECT_EQ(view.selectedText(), QStringLiteral("alpha"));

  ASSERT_TRUE(view.findText(QStringLiteral("alpha"), QTextDocument::FindFlags(),
                            false, &wrapped, &index, &count));
  EXPECT_EQ(index, 1);
  EXPECT_FALSE(wrapped);
  ASSERT_TRUE(view.findText(QStringLiteral("alpha"), QTextDocument::FindFlags(),
                            false, &wrapped, &index, &count));
  EXPECT_EQ(index, 2);
  ASSERT_TRUE(view.findText(QStringLiteral("alpha"), QTextDocument::FindFlags(),
                            false, &wrapped, &index, &count));
  EXPECT_EQ(index, 0);
  EXPECT_TRUE(wrapped);

  ASSERT_TRUE(view.findText(QStringLiteral("alpha"),
                            QTextDocument::FindBackward, false, &wrapped,
                            &index, &count));
  EXPECT_EQ(index, 2);
  EXPECT_TRUE(wrapped);

  EXPECT_TRUE(view.setCurrentSearchMatch(1));
  EXPECT_EQ(view.currentSearchMatch(), 1);
  EXPECT_EQ(view.selectedText(), QStringLiteral("alpha"));
}
