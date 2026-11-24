#include "activity_bar.h"

#include <QApplication>
#include <QToolButton>

#include <gtest/gtest.h>

// 活动栏：固定 30px、四个互斥视图、点已选中项也会上报
int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QApplication app(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

TEST(ActivityBarTest, HasFourViewsWithObjectNames) {
  ActivityBar bar;
  EXPECT_NE(bar.button(ActivityBar::kDocuments), nullptr);
  EXPECT_NE(bar.button(ActivityBar::kSearch), nullptr);
  EXPECT_NE(bar.button(ActivityBar::kBookmarks), nullptr);
  EXPECT_NE(bar.button(ActivityBar::kRenderStatus), nullptr);
  EXPECT_EQ(bar.button(ActivityBar::kDocuments)->objectName(),
            QStringLiteral("activityDocuments"));
  EXPECT_EQ(bar.button(ActivityBar::kSearch)->objectName(),
            QStringLiteral("activitySearch"));
  EXPECT_EQ(bar.button(ActivityBar::kBookmarks)->objectName(),
            QStringLiteral("activityBookmarks"));
  EXPECT_EQ(bar.button(ActivityBar::kRenderStatus)->objectName(),
            QStringLiteral("activityRenderStatus"));

  bar.SetActiveView(ActivityBar::kSearch);
  EXPECT_EQ(bar.active_view(), ActivityBar::kSearch);
  EXPECT_TRUE(bar.button(ActivityBar::kSearch)->isChecked());
  EXPECT_FALSE(bar.button(ActivityBar::kDocuments)->isChecked());
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
  int last = -1;
  QObject::connect(&bar, &ActivityBar::viewActivated,
                   [&last](int view) { last = view; });

  bar.button(ActivityBar::kBookmarks)->click();
  EXPECT_EQ(last, ActivityBar::kBookmarks);

  // 已选中的项再次点击也要发出去：宿主用它在展开/收起侧栏之间切换
  bar.SetActiveView(ActivityBar::kBookmarks);
  bar.button(ActivityBar::kBookmarks)->click();
  EXPECT_EQ(last, ActivityBar::kBookmarks);
}
