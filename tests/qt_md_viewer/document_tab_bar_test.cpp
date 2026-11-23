#include "document_tab_bar.h"

#include <QApplication>

#include <gtest/gtest.h>

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QApplication app(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

TEST(DocumentTabBarTest, ClampsTabWidthAndHeight) {
  DocumentTabBar bar;
  bar.addTab(QStringLiteral("一个很长很长的文档名字用来撑宽标签.md"));
  bar.addTab(QStringLiteral("b.md"));
  for (int i = 0; i < bar.count(); ++i) {
    const QSize size = bar.tabSizeHint(i);
    EXPECT_EQ(size.height(), 34);
    EXPECT_GE(size.width(), 120);
    EXPECT_LE(size.width(), 220);
    EXPECT_EQ(bar.minimumTabSizeHint(i).width(), 120);
    EXPECT_EQ(bar.minimumTabSizeHint(i).height(), 34);
  }
}

TEST(DocumentTabBarTest, ChromeColorsSeparateSelectedFromFrame) {
  const TabStripColors light = TabStripColorsFor(false);
  const TabStripColors dark = TabStripColorsFor(true);
  // 选中标签必须与标题栏/标签条底色不同
  EXPECT_NE(light.selected, light.background);
  EXPECT_NE(dark.selected, dark.background);
  EXPECT_NE(light.selected_text, light.normal_text);
  EXPECT_NE(dark.selected_text, dark.normal_text);
}

TEST(DocumentTabBarTest, WelcomeTabHasNoCloseButton) {
  DocumentTabBar bar;
  bar.addTab(QStringLiteral("欢迎"));
  bar.RemoveCloseButton(0);
  bar.addTab(QStringLiteral("a.md"));
  EXPECT_EQ(bar.tabButton(0, QTabBar::RightSide), nullptr);
  EXPECT_NE(bar.tabButton(1, QTabBar::RightSide), nullptr);
}
