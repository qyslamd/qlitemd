#include "welcome_bar.h"

#include <QApplication>
#include <QLabel>
#include <QLineEdit>
#include <QToolButton>

#include <gtest/gtest.h>

// 欢迎态专用工具栏：没有文档级控件，也没有搜索框
int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QApplication app(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

TEST(WelcomeBarTest, IsAFixedStripWithoutDocumentControls) {
  WelcomeBar bar;
  EXPECT_EQ(bar.objectName(), QStringLiteral("welcomeBar"));
  EXPECT_EQ(bar.minimumHeight(), 32);
  EXPECT_EQ(bar.maximumHeight(), 32);

  // 搜索框属于侧栏的工作区搜索，欢迎栏不提供
  EXPECT_EQ(bar.findChild<QLineEdit*>(), nullptr);
  EXPECT_NE(bar.findChild<QLabel*>(QStringLiteral("welcomeTitle")), nullptr);
  EXPECT_NE(bar.findChild<QLabel*>(QStringLiteral("welcomeWorkspaceLabel")),
            nullptr);
  EXPECT_NE(bar.quick_open_button(), nullptr);
  EXPECT_NE(bar.settings_button(), nullptr);
}

TEST(WelcomeBarTest, ShowsWorkspaceNameOrPlaceholder) {
  WelcomeBar bar;
  EXPECT_EQ(bar.workspace_label()->text(), QStringLiteral("未打开工作区"));
  EXPECT_TRUE(bar.workspace_label()->toolTip().isEmpty());

  bar.SetWorkspaceName(QStringLiteral("D:/workspace/demo"));
  EXPECT_EQ(bar.workspace_label()->text(), QStringLiteral("demo"));
  EXPECT_FALSE(bar.workspace_label()->toolTip().isEmpty());

  bar.SetWorkspaceName(QString());
  EXPECT_EQ(bar.workspace_label()->text(), QStringLiteral("未打开工作区"));
}

TEST(WelcomeBarTest, ButtonsForwardToTheWindow) {
  WelcomeBar bar;
  int quick_open = 0;
  int settings = 0;
  QObject::connect(&bar, &WelcomeBar::quickOpenRequested,
                   [&quick_open] { ++quick_open; });
  QObject::connect(&bar, &WelcomeBar::settingsRequested,
                   [&settings] { ++settings; });

  bar.quick_open_button()->click();
  bar.settings_button()->click();
  EXPECT_EQ(quick_open, 1);
  EXPECT_EQ(settings, 1);
}

TEST(WelcomeBarTest, HidesQuickOpenWhenNarrow) {
  WelcomeBar bar;
  bar.resize(600, 32);
  bar.show();
  QApplication::processEvents();
  EXPECT_TRUE(bar.quick_open_button()->isVisible());

  bar.resize(400, 32);
  QApplication::processEvents();
  EXPECT_FALSE(bar.quick_open_button()->isVisible());
  EXPECT_TRUE(bar.settings_button()->isVisible());
}

TEST(WelcomeBarTest, ThemeSwitchKeepsTheBarUsable) {
  WelcomeBar bar;
  bar.SetThemeDark(true);
  EXPECT_TRUE(bar.quick_open_button()->isEnabled());
  EXPECT_TRUE(bar.settings_button()->isEnabled());
  bar.SetThemeDark(false);
  EXPECT_TRUE(bar.quick_open_button()->isEnabled());
}
