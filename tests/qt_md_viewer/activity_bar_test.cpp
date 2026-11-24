#include "activity_bar.h"

#include <QApplication>
#include <QToolButton>

#include <gtest/gtest.h>

// 活动栏：固定 30px、四个共享 SideView 的互斥视图、点已选中项也会上报
int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QApplication app(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

TEST(ActivityBarTest, HasFourViewsWithObjectNames) {
  ActivityBar bar;
  EXPECT_NE(bar.button(SideView::kDocuments), nullptr);
  EXPECT_NE(bar.button(SideView::kSearch), nullptr);
  EXPECT_NE(bar.button(SideView::kBookmarks), nullptr);
  EXPECT_NE(bar.button(SideView::kRenderStatus), nullptr);
  EXPECT_EQ(bar.button(SideView::kDocuments)->objectName(),
            QStringLiteral("activityDocuments"));
  EXPECT_EQ(bar.button(SideView::kSearch)->objectName(),
            QStringLiteral("activitySearch"));
  EXPECT_EQ(bar.button(SideView::kBookmarks)->objectName(),
            QStringLiteral("activityBookmarks"));
  EXPECT_EQ(bar.button(SideView::kRenderStatus)->objectName(),
            QStringLiteral("activityRenderStatus"));

  bar.SetActiveView(SideView::kSearch);
  EXPECT_EQ(bar.active_view(), SideView::kSearch);
  EXPECT_TRUE(bar.button(SideView::kSearch)->isChecked());
  EXPECT_FALSE(bar.button(SideView::kDocuments)->isChecked());
}

TEST(ActivityBarTest, SettingsButtonSitsAtTheBottom) {
  ActivityBar bar;
  auto* settings = bar.settings_button();
  ASSERT_NE(settings, nullptr);
  EXPECT_EQ(settings->objectName(), QStringLiteral("activitySettings"));
  EXPECT_FALSE(settings->isCheckable());
  int requested = 0;
  QObject::connect(&bar, &ActivityBar::settingsRequested,
                   [&requested] { ++requested; });
  settings->click();
  EXPECT_EQ(requested, 1);
}

TEST(ActivityBarTest, AnnouncesClicksEvenWhenAlreadyActive) {
  ActivityBar bar;
  SideView last = SideView::kDocuments;
  QObject::connect(&bar, &ActivityBar::viewActivated,
                   [&last](SideView view) { last = view; });

  bar.button(SideView::kBookmarks)->click();
  EXPECT_EQ(last, SideView::kBookmarks);

  // 已选中的项再次点击也要发出去：宿主用它在展开/收起侧栏之间切换
  bar.SetActiveView(SideView::kBookmarks);
  bar.button(SideView::kBookmarks)->click();
  EXPECT_EQ(last, SideView::kBookmarks);
}
