#include "session_controller.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QStandardPaths>

#include <gtest/gtest.h>

#include "md_viewer_settings.h"

int main(int argc, char** argv) {
  QStandardPaths::setTestModeEnabled(true);
  QCoreApplication::setOrganizationName(QStringLiteral("md-tests"));
  QCoreApplication::setApplicationName(
      QStringLiteral("session-controller-%1")
          .arg(QDateTime::currentMSecsSinceEpoch()));
  QCoreApplication app(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

TEST(SessionControllerTest, GeometryAndShellLayoutRoundTrip) {
  MdViewerSettings settings;
  SessionController controller(&settings, /*session_owner=*/true);

  ShellLayoutState layout;
  layout.view = SideView::kSearch;
  layout.sidebar_visible = false;
  layout.sidebar_width = 333;
  layout.outline_width = 222;
  layout.outline_visible = false;
  controller.SaveGeometry(QByteArrayLiteral("geometry"));
  controller.SaveShellLayout(layout);
  controller.SaveAll();

  MdViewerSettings reloaded = settings;
  SessionController second(&reloaded, /*session_owner=*/true);
  EXPECT_EQ(second.SavedGeometry(), QByteArrayLiteral("geometry"));
  const ShellLayoutState saved = second.SavedShellLayout();
  EXPECT_EQ(saved.view, SideView::kSearch);
  EXPECT_FALSE(saved.sidebar_visible);
  EXPECT_EQ(saved.sidebar_width, 333);
  EXPECT_EQ(saved.outline_width, 222);
  EXPECT_FALSE(saved.outline_visible);
}

TEST(SessionControllerTest, SaveTabsWritesCodecAndLastFile) {
  MdViewerSettings settings;
  SessionController controller(&settings, /*session_owner=*/true);

  controller.SaveTabs({QString(), QStringLiteral("C:/docs/a.md"),
                       QStringLiteral("C:/docs/b.md")},
                      2);

  EXPECT_EQ(controller.SavedLastFile(), QStringLiteral("C:/docs/b.md"));
  int active = -1;
  const QVector<QString> paths = controller.SavedTabPaths(&active);
  ASSERT_EQ(paths.size(), 3);
  EXPECT_TRUE(paths.at(0).isEmpty());
  EXPECT_EQ(paths.at(1), QStringLiteral("C:/docs/a.md"));
  EXPECT_EQ(paths.at(2), QStringLiteral("C:/docs/b.md"));
  EXPECT_EQ(active, 2);
}

TEST(SessionControllerTest, MissingTabsReportNoSession) {
  MdViewerSettings settings;
  SessionController controller(&settings, /*session_owner=*/true);

  int active = 3;
  EXPECT_TRUE(controller.SavedTabPaths(&active).isEmpty());
  EXPECT_EQ(active, 0);
}

TEST(SessionControllerTest, WorkspaceFlagsFollowOpenAndClear) {
  MdViewerSettings settings;
  settings.workspace_closed = true;
  SessionController controller(&settings, /*session_owner=*/true);

  EXPECT_FALSE(controller.RestoreRequested());
  controller.MarkWorkspaceOpened(QStringLiteral("C:/docs/a.md"));
  EXPECT_TRUE(controller.RestoreRequested());
  EXPECT_EQ(controller.SavedLastFile(), QStringLiteral("C:/docs/a.md"));

  controller.SetRoot(QStringLiteral("C:/docs"));
  EXPECT_EQ(controller.SavedRoot(), QStringLiteral("C:/docs"));

  controller.ClearWorkspace();
  EXPECT_FALSE(controller.RestoreRequested());
  EXPECT_TRUE(controller.SavedRoot().isEmpty());
  EXPECT_TRUE(controller.SavedLastFile().isEmpty());
  EXPECT_TRUE(controller.SavedTabPaths().isEmpty());
}

TEST(SessionControllerTest, SidebarAndOutlineSettersUpdateTheSnapshot) {
  MdViewerSettings settings;
  SessionController controller(&settings, /*session_owner=*/true);

  controller.SetSidebarView(SideView::kBookmarks);
  controller.SetSidebarVisible(false);
  controller.SetOutlineVisible(false);

  const ShellLayoutState layout = controller.SavedShellLayout();
  EXPECT_EQ(layout.view, SideView::kBookmarks);
  EXPECT_FALSE(layout.sidebar_visible);
  EXPECT_FALSE(layout.outline_visible);
}

TEST(SessionControllerTest, SecondaryWindowDoesNotWrite) {
  MdViewerSettings settings;
  const QByteArray original_tabs = settings.open_tabs_json;
  SessionController controller(&settings, /*session_owner=*/false);

  controller.SaveGeometry(QByteArrayLiteral("ignored"));
  controller.SaveTabs({QString(), QStringLiteral("C:/docs/a.md")}, 1);
  controller.SaveAll();

  EXPECT_TRUE(settings.window_geometry.isEmpty());
  EXPECT_EQ(settings.open_tabs_json, original_tabs);
}
