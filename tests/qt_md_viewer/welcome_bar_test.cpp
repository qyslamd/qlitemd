#include <QApplication>
#include <QLabel>
#include <QLineEdit>
#include <QToolButton>

#include <gtest/gtest.h>

#include "test_i18n_helper.h"
#include "top_tool_bar.h"

// 欢迎态专用工具栏：没有文档级控件，也没有搜索框
int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QApplication app(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  InstallTestTranslator();
  return RUN_ALL_TESTS();
}

TEST(WelcomeBarTest, IsAFixedStripWithoutDocumentControls) {
  WelcomeBar bar;
  EXPECT_EQ(bar.objectName(), QStringLiteral("welcomeBar"));

  // 搜索框属于侧栏的工作区搜索，欢迎栏不提供
  EXPECT_EQ(bar.findChild<QLineEdit*>(), nullptr);
  EXPECT_NE(bar.findChild<QLabel*>(QStringLiteral("welcomeTitle")), nullptr);
  EXPECT_NE(bar.findChild<QLabel*>(QStringLiteral("welcomeWorkspaceLabel")),
            nullptr);
  EXPECT_NE(bar.quick_open_button(), nullptr);
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
  QObject::connect(&bar, &WelcomeBar::quickOpenRequested,
                   [&quick_open] { ++quick_open; });

  bar.quick_open_button()->click();
  EXPECT_EQ(quick_open, 1);
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
}
