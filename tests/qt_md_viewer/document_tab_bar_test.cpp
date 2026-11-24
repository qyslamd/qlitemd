#include "document_tab_bar.h"

#include <QApplication>

#include <gtest/gtest.h>

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QApplication app(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

TEST(DocumentTabBarTest, LoadingStateRoundTrips) {
  DocumentTabBar bar;
  bar.addTab(QStringLiteral("a.md"));
  EXPECT_FALSE(bar.IsTabLoading(0));
  bar.SetTabLoading(0, true);
  EXPECT_TRUE(bar.IsTabLoading(0));
  bar.SetTabLoading(0, false);
  EXPECT_FALSE(bar.IsTabLoading(0));
}

TEST(DocumentTabBarTest, CloseButtonHasMinimumHitArea) {
  DocumentTabBar bar;
  bar.addTab(QStringLiteral("a.md"));
  QWidget* close = bar.tabButton(0, QTabBar::RightSide);
  ASSERT_NE(close, nullptr);
  EXPECT_GE(close->minimumSize().width(), 18);
  EXPECT_GE(close->minimumSize().height(), 18);
}

TEST(DocumentTabBarTest, WelcomeTabHasNoCloseButton) {
  DocumentTabBar bar;
  bar.addTab(QStringLiteral("欢迎"));
  bar.RemoveCloseButton(0);
  bar.addTab(QStringLiteral("a.md"));
  EXPECT_EQ(bar.tabButton(0, QTabBar::RightSide), nullptr);
  EXPECT_NE(bar.tabButton(1, QTabBar::RightSide), nullptr);
}
