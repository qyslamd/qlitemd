#include "md_viewer_settings.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QStandardPaths>

#include <gtest/gtest.h>

// 会话语义测试：卸载工作区靠 session/workspace_closed 标志把
// "用户关了" 与 "首次运行" 区分开——重启时窗口见标志直接回欢迎页。
// 这里验证标志与 root/last_file 一起落盘、能完整往返。
namespace {

// 测试模式下 AppConfigLocation 落到用户目录的 .qttest 下，再配独立的
// org/app 名，配置文件不会碰真实用户数据
class QCoreApplicationGuard {
 public:
  QCoreApplicationGuard() {
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setOrganizationName(QStringLiteral("qlitemd-tests"));
    QCoreApplication::setApplicationName(
        QStringLiteral("md_viewer_settings_test"));
    // QCoreApplication 保存 argv 指针与 argc 引用，得让它们活得和 app 一样久
    static char program_name[] = "md_viewer_settings_test";
    static char* argv[] = {program_name};
    static int argc = 1;
    app_ = new QCoreApplication(argc, argv);
  }

 private:
  QCoreApplication* app_ = nullptr;
};

QCoreApplicationGuard& AppGuard() {
  static QCoreApplicationGuard guard;
  return guard;
}

// 配置文件是同一个；每个用例先删掉它，Load 才是确定的初始状态
void ResetConfigFile() { QFile::remove(MdViewerSettings::DefaultPath()); }

}  // namespace

TEST(MdViewerSettingsTest, FreshConfigHasOpenWorkspace) {
  AppGuard();
  ResetConfigFile();
  MdViewerSettings settings;
  settings.Load();
  EXPECT_FALSE(settings.workspace_closed);
  EXPECT_TRUE(settings.root_dir.isEmpty());
  EXPECT_TRUE(settings.last_file.isEmpty());
}

TEST(MdViewerSettingsTest, SaveSessionRoundTripsWorkspaceFields) {
  AppGuard();
  ResetConfigFile();
  MdViewerSettings settings;
  settings.Load();
  settings.root_dir = QStringLiteral("D:/docs");
  settings.last_file = QStringLiteral("D:/docs/readme.md");
  settings.workspace_closed = false;
  settings.SaveSession();

  MdViewerSettings reloaded;
  reloaded.Load();
  EXPECT_EQ(reloaded.root_dir, QStringLiteral("D:/docs"));
  EXPECT_EQ(reloaded.last_file, QStringLiteral("D:/docs/readme.md"));
  EXPECT_FALSE(reloaded.workspace_closed);

  // 卸载：root/last 清空、标志置位，重启侧据此回欢迎页而不是恢复最近
  reloaded.root_dir.clear();
  reloaded.last_file.clear();
  reloaded.workspace_closed = true;
  reloaded.SaveSession();

  MdViewerSettings after_unload;
  after_unload.Load();
  EXPECT_TRUE(after_unload.root_dir.isEmpty());
  EXPECT_TRUE(after_unload.last_file.isEmpty());
  EXPECT_TRUE(after_unload.workspace_closed);
}

TEST(MdViewerSettingsTest, FullSavePreservesWorkspaceClosed) {
  AppGuard();
  ResetConfigFile();
  MdViewerSettings settings;
  settings.Load();
  settings.workspace_closed = true;
  settings.Save();

  MdViewerSettings reloaded;
  reloaded.Load();
  EXPECT_TRUE(reloaded.workspace_closed);
}

TEST(MdViewerSettingsTest, OpeningWorkspaceClearsClosedFlag) {
  AppGuard();
  ResetConfigFile();
  MdViewerSettings settings;
  settings.Load();
  settings.workspace_closed = true;
  settings.SaveSession();

  // 窗口侧的约定：成功 SetRootDir（打开目录/打开文件都经过它）即视为
  // 重新有了工作区，解除标志。这里按窗口会写下的状态验证持久层
  MdViewerSettings reopened;
  reopened.Load();
  reopened.root_dir = QStringLiteral("D:/other");
  reopened.last_file = QStringLiteral("D:/other/intro.md");
  reopened.workspace_closed = false;
  reopened.SaveSession();

  MdViewerSettings final_state;
  final_state.Load();
  EXPECT_FALSE(final_state.workspace_closed);
  EXPECT_EQ(final_state.root_dir, QStringLiteral("D:/other"));
}

TEST(MdViewerSettingsTest, SaveSessionRoundTripsOpenTabs) {
  AppGuard();
  ResetConfigFile();
  MdViewerSettings settings;
  settings.Load();
  settings.open_tabs_json =
      QByteArrayLiteral("{\"tabs\":[\"\",\"/a\",\"/b\"],\"active\":2}");
  settings.SaveSession();

  MdViewerSettings reloaded;
  reloaded.Load();
  EXPECT_EQ(reloaded.open_tabs_json, settings.open_tabs_json);
}

TEST(MdViewerSettingsTest, RecentRootsDedupeAndCap) {
  AppGuard();
  ResetConfigFile();
  MdViewerSettings settings;
  settings.Load();
  // AddRecentRoot 会把相对路径补成绝对路径，这里用根目录拼出跨平台
  // 都保持原样的绝对路径
  const QString base = QDir::rootPath();
  for (int i = 0; i < 10; ++i) {
    settings.AddRecentRoot(base + QStringLiteral("root%1").arg(i));
  }
  const QString duplicate = base + QStringLiteral("root5");
  settings.AddRecentRoot(duplicate);
  EXPECT_EQ(settings.recent_roots.first(), duplicate);
  EXPECT_EQ(settings.recent_roots.size(), 8);
  EXPECT_EQ(settings.recent_roots.count(duplicate), 1);

  settings.Save();
  MdViewerSettings reloaded;
  reloaded.Load();
  EXPECT_EQ(reloaded.recent_roots, settings.recent_roots);
}

TEST(MdViewerSettingsTest, ReadOnlyCopyDoesNotWrite) {
  AppGuard();
  ResetConfigFile();
  MdViewerSettings settings;
  settings.Load();
  // 次窗口拿到的是只读副本：任何写入口都不能落盘
  settings.read_only = true;
  settings.AddRecentFile(QStringLiteral("D:/x.md"));
  settings.AddRecentRoot(QStringLiteral("D:/r"));
  settings.root_dir = QStringLiteral("D:/r");
  settings.open_tabs_json = QByteArrayLiteral("{\"tabs\":[\"/x\"]}");
  settings.SaveSession();
  settings.Save();

  MdViewerSettings reloaded;
  reloaded.Load();
  EXPECT_TRUE(reloaded.recent_files.isEmpty());
  EXPECT_TRUE(reloaded.recent_roots.isEmpty());
  EXPECT_TRUE(reloaded.root_dir.isEmpty());
  EXPECT_TRUE(reloaded.open_tabs_json.isEmpty());
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
