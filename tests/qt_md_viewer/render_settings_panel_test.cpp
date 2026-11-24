#include "render_settings_panel.h"

#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QListWidget>
#include <QPushButton>
#include <QStackedWidget>
#include <QStandardPaths>

#include <gtest/gtest.h>

#include "markdown_view.h"
#include "settings_dialog.h"

// 设置对话框的分组布局：左侧分类 + 右侧分页，控件仍按 objectName 可达
int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QStandardPaths::setTestModeEnabled(true);
  QCoreApplication::setOrganizationName(QStringLiteral("md-tests"));
  QCoreApplication::setApplicationName(
      QStringLiteral("settings-panel-%1")
          .arg(QDateTime::currentMSecsSinceEpoch()));
  QApplication app(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  const int result = RUN_ALL_TESTS();
  QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation) +
       QStringLiteral("/markdown-render"))
      .removeRecursively();
  return result;
}

TEST(RenderSettingsPanelLayoutTest, HasCategoryNavAndPages) {
  MarkdownView view;
  RenderSettingsPanel panel(&view);

  auto* categories =
      panel.findChild<QListWidget*>(QStringLiteral("settingsCategoryList"));
  auto* pages =
      panel.findChild<QStackedWidget*>(QStringLiteral("settingsPageStack"));
  ASSERT_NE(categories, nullptr);
  ASSERT_NE(pages, nullptr);
  EXPECT_EQ(categories->count(), 5);
  EXPECT_EQ(pages->count(), 5);

  categories->setCurrentRow(2);
  EXPECT_EQ(pages->currentIndex(), 2);

  for (const char* page :
       {"settingsPageAppearance", "settingsPageReading", "settingsPageContent",
        "settingsPageCache", "settingsPageDiagnostics"}) {
    EXPECT_NE(panel.findChild<QWidget*>(QString::fromLatin1(page)), nullptr)
        << page;
  }
  EXPECT_NE(panel.findChild<QWidget*>(QStringLiteral("themeCombo")), nullptr);
  EXPECT_NE(panel.findChild<QWidget*>(QStringLiteral("codeLineNumbersCheck")),
            nullptr);
  EXPECT_NE(panel.findChild<QWidget*>(QStringLiteral("persistentCacheCheck")),
            nullptr);
  EXPECT_NE(panel.findChild<QWidget*>(QStringLiteral("traceCheck")), nullptr);
}

TEST(SettingsDialogTest, HasCloseButton) {
  MarkdownView view;
  // 面板会被对话框接管所有权，必须与生产路径一样在堆上创建
  auto* panel = new RenderSettingsPanel(&view);
  SettingsDialog dialog(panel);
  auto* close =
      dialog.findChild<QPushButton*>(QStringLiteral("settingsCloseButton"));
  EXPECT_NE(close, nullptr);
  EXPECT_EQ(close->text(), QStringLiteral("关闭"));
}
