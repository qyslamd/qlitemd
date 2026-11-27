#include "settings_panel.h"

#include <QApplication>
#include <QCheckBox>
#include <QDateTime>
#include <QDir>
#include <QListWidget>
#include <QPushButton>
#include <QSignalSpy>
#include <QStackedWidget>
#include <QStandardPaths>

#include <gtest/gtest.h>

#include "markdown_view.h"
#include "shell_layout.h"

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

TEST(SettingsPanelLayoutTest, HasCategoryNavAndPages) {
  MarkdownView view;
  SettingsPanel panel(&view);

  auto* categories =
      panel.findChild<QListWidget*>(QStringLiteral("settingsCategoryList"));
  auto* pages =
      panel.findChild<QStackedWidget*>(QStringLiteral("settingsPageStack"));
  ASSERT_NE(categories, nullptr);
  ASSERT_NE(pages, nullptr);
  EXPECT_EQ(categories->count(), 6);
  EXPECT_EQ(pages->count(), 6);

  categories->setCurrentRow(2);
  EXPECT_EQ(pages->currentIndex(), 2);

  for (const char* page : {"settingsPageGeneral", "settingsPageAppearance",
                           "settingsPageReading", "settingsPageContent",
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

TEST(ShellLayoutTest, DefaultMetricsKeepThreeColumnsBalanced) {
  const ShellMetrics wide = DefaultShellMetrics(1240, true);
  EXPECT_EQ(wide.sidebar_width, 298);
  EXPECT_EQ(wide.outline_width, 223);

  const ShellMetrics min_wide = DefaultShellMetrics(980, true);
  EXPECT_EQ(min_wide.sidebar_width, 240);
  EXPECT_EQ(min_wide.outline_width, 200);

  const ShellMetrics narrow = DefaultShellMetrics(600, false);
  EXPECT_EQ(narrow.sidebar_width, 220);
  EXPECT_EQ(narrow.outline_width, 0);
}

TEST(SettingsPanelLayoutTest, GeneralPageExposesResetActions) {
  MarkdownView view;
  SettingsPanel panel(&view);
  auto* layout_button =
      panel.findChild<QPushButton*>(QStringLiteral("resetLayoutButton"));
  auto* preferences_button =
      panel.findChild<QPushButton*>(QStringLiteral("resetPreferencesButton"));
  ASSERT_NE(layout_button, nullptr);
  ASSERT_NE(preferences_button, nullptr);

  QSignalSpy layout_requested(&panel, &SettingsPanel::resetLayoutRequested);
  QSignalSpy preferences_requested(&panel,
                                   &SettingsPanel::resetPreferencesRequested);
  layout_button->click();
  preferences_button->click();
  EXPECT_EQ(layout_requested.count(), 1);
  EXPECT_EQ(preferences_requested.count(), 1);
}

TEST(RenderSettingsPanelLayoutTest, SurvivesViewDestruction) {
  // 关闭标签会销毁视图；面板持有 QPointer，槽里必须安全空转
  auto* view = new MarkdownView();
  SettingsPanel panel(view);
  delete view;

  auto* line_numbers =
      panel.findChild<QCheckBox*>(QStringLiteral("codeLineNumbersCheck"));
  ASSERT_NE(line_numbers, nullptr);
  line_numbers->setChecked(!line_numbers->isChecked());
  QApplication::processEvents();
}
