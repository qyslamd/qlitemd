#include "reader_page.h"

#include <QApplication>
#include <QLabel>
#include <QToolButton>
#include <QWidget>

#include <gtest/gtest.h>

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QApplication app(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

TEST(ReaderPageTest, BuildsProgressAndContent) {
  auto* content = new QWidget;
  ReaderPage page(content);
  ASSERT_NE(page.progress(), nullptr);
  ASSERT_NE(page.back_to_top(), nullptr);
  EXPECT_EQ(content->parentWidget(), &page);
  EXPECT_TRUE(page.back_to_top()->isHidden());
  EXPECT_FALSE(page.progress()->isHidden());
}

TEST(ReaderPageTest, PositionsBackToTopOnResize) {
  auto* content = new QWidget;
  ReaderPage page(content);
  page.resize(400, 300);
  page.show();
  page.back_to_top()->show();
  QApplication::processEvents();
  // 22px 边距，38px 固定尺寸；位置按页面自身尺寸算
  EXPECT_EQ(page.back_to_top()->x(), 400 - 38 - 22);
  EXPECT_EQ(page.back_to_top()->y(), 300 - 38 - 22);
}

TEST(ReadingProgressTest, ClampsToUnitRange) {
  ReadingProgressBar bar;
  bar.SetProgress(0.42);
  EXPECT_DOUBLE_EQ(bar.progress(), 0.42);
  bar.SetProgress(-1.0);
  EXPECT_DOUBLE_EQ(bar.progress(), 0.0);
  bar.SetProgress(3.0);
  EXPECT_DOUBLE_EQ(bar.progress(), 1.0);
}
