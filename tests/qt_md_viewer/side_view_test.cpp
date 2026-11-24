#include "side_view.h"

#include <gtest/gtest.h>

// SideView 的数值会被写进 qt_md_viewer.ini，改动等于破坏旧会话
TEST(SideViewTest, IdsAreStableForPersistence) {
  EXPECT_EQ(static_cast<int>(SideView::kDocuments), 0);
  EXPECT_EQ(static_cast<int>(SideView::kSearch), 1);
  EXPECT_EQ(static_cast<int>(SideView::kBookmarks), 2);
  EXPECT_EQ(static_cast<int>(SideView::kRenderStatus), 3);
  EXPECT_EQ(kSideViewCount, 4);
}

TEST(SideViewTest, ConvertsAndRejectsOutOfRangeValues) {
  EXPECT_TRUE(IsValidSideView(0));
  EXPECT_TRUE(IsValidSideView(kSideViewCount - 1));
  EXPECT_FALSE(IsValidSideView(-1));
  EXPECT_FALSE(IsValidSideView(kSideViewCount));

  EXPECT_EQ(SideViewFromInt(2), SideView::kBookmarks);
  EXPECT_EQ(SideViewFromInt(9), SideView::kDocuments);
  EXPECT_EQ(SideViewFromInt(-1, SideView::kSearch), SideView::kSearch);
}

TEST(SideViewTest, TitlesMatchTheActivityBar) {
  EXPECT_EQ(SideViewTitle(SideView::kDocuments), QStringLiteral("文档"));
  EXPECT_EQ(SideViewTitle(SideView::kSearch), QStringLiteral("搜索"));
  EXPECT_EQ(SideViewTitle(SideView::kBookmarks), QStringLiteral("收藏"));
  EXPECT_EQ(SideViewTitle(SideView::kRenderStatus), QStringLiteral("渲染状态"));
}
