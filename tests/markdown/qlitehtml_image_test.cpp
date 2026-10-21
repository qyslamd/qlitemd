// qlitehtml 图片命中与选区公开入口的回归：宿主靠 imageAt() 判断右键/左键
// 落在哪张图上，靠 clearSelection() 在搜索框清空时清掉选区。
#include <gtest/gtest.h>

#include <QApplication>
#include <QContextMenuEvent>
#include <QMouseEvent>
#include <QPoint>
#include <QSignalSpy>
#include <QTest>
#include <QTextDocument>
#include <QUrl>

#include "qlitehtmlwidget.h"

class TestLiteHtmlWidget final : public QLiteHtmlWidget {
 public:
  using QLiteHtmlWidget::contextMenuEvent;
  using QLiteHtmlWidget::mousePressEvent;
  using QLiteHtmlWidget::mouseReleaseEvent;
};

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QApplication app(argc, argv);
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

namespace {

QPoint FindImagePoint(QLiteHtmlWidget* view) {
  for (int y = 0; y < view->viewport()->height(); y += 2) {
    for (int x = 0; x < view->viewport()->width(); x += 2) {
      if (!view->imageAt(QPoint(x, y)).isEmpty()) {
        return QPoint(x, y);
      }
    }
  }
  return QPoint(-1, -1);
}

void ShowImageHtml(QLiteHtmlWidget* view, const QString& html) {
  view->resize(400, 300);
  view->setResourceHandler([](const QUrl&) { return QByteArray(); });
  view->setPixmapCallBack([](const QUrl&) { return QPixmap(); });
  view->setHtml(html);
  view->show();
  QTest::qWait(80);
}

TEST(QliteHtmlImageTest, ImageAtClickAndContextMenu) {
  TestLiteHtmlWidget view;
  ShowImageHtml(&view,
                QStringLiteral("<body style=\"margin:0\"><img src=\"foo.png\" "
                               "width=\"40\" height=\"40\"></body>"));
  const QPoint point = FindImagePoint(&view);
  ASSERT_GE(point.x(), 0);
  const QPoint parent_pos = view.viewport()->mapToParent(point);

  QSignalSpy clicked(&view, &QLiteHtmlWidget::imageClicked);
  QMouseEvent press(QEvent::MouseButtonPress, parent_pos, Qt::LeftButton,
                    Qt::LeftButton, Qt::NoModifier);
  QMouseEvent release(QEvent::MouseButtonRelease, parent_pos, Qt::LeftButton,
                      Qt::NoButton, Qt::NoModifier);
  view.mousePressEvent(&press);
  view.mouseReleaseEvent(&release);
  QTest::qWait(30);
  ASSERT_EQ(clicked.count(), 1);
  EXPECT_TRUE(
      clicked.at(0).at(0).toUrl().path().endsWith(QStringLiteral("foo.png")));

  QSignalSpy menu(&view, &QLiteHtmlWidget::imageContextMenuRequested);
  QContextMenuEvent event(QContextMenuEvent::Mouse, parent_pos,
                          view.viewport()->mapToGlobal(point));
  view.contextMenuEvent(&event);
  EXPECT_EQ(menu.count(), 1);
}

TEST(QliteHtmlImageTest, ClearSelectionMakesSelectedTextEmpty) {
  QLiteHtmlWidget view;
  ShowImageHtml(&view, QStringLiteral("<p>hello world</p>"));
  ASSERT_TRUE(view.findText(QStringLiteral("hello"), QTextDocument::FindFlags(),
                            false));
  EXPECT_FALSE(view.selectedText().isEmpty());
  view.clearSelection();
  EXPECT_TRUE(view.selectedText().isEmpty());
}

TEST(QliteHtmlImageTest, LinkedImageDoesNotEmitImageClick) {
  TestLiteHtmlWidget view;
  ShowImageHtml(
      &view,
      QStringLiteral(
          "<body style=\"margin:0\"><a href=\"https://example.com\">"
          "<img src=\"foo.png\" width=\"40\" height=\"40\"></a></body>"));
  const QPoint point = FindImagePoint(&view);
  ASSERT_GE(point.x(), 0);
  const QPoint parent_pos = view.viewport()->mapToParent(point);

  QSignalSpy image_clicked(&view, &QLiteHtmlWidget::imageClicked);
  QSignalSpy link_clicked(&view, &QLiteHtmlWidget::linkClicked);
  QMouseEvent press(QEvent::MouseButtonPress, parent_pos, Qt::LeftButton,
                    Qt::LeftButton, Qt::NoModifier);
  QMouseEvent release(QEvent::MouseButtonRelease, parent_pos, Qt::LeftButton,
                      Qt::NoButton, Qt::NoModifier);
  view.mousePressEvent(&press);
  view.mouseReleaseEvent(&release);
  QTest::qWait(30);
  EXPECT_EQ(image_clicked.count(), 0);
  EXPECT_EQ(link_clicked.count(), 1);
}

}  // namespace
