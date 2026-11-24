#include "bookmark_controller.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QStandardPaths>

#include <gtest/gtest.h>

#include "md_viewer_settings.h"

int main(int argc, char** argv) {
  QStandardPaths::setTestModeEnabled(true);
  QCoreApplication::setOrganizationName(QStringLiteral("md-tests"));
  QCoreApplication::setApplicationName(
      QStringLiteral("bookmark-controller-%1")
          .arg(QDateTime::currentMSecsSinceEpoch()));
  QCoreApplication app(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

TEST(BookmarkControllerTest, ToggleSavesAndReloads) {
  MdViewerSettings settings;
  BookmarkController controller(&settings);
  controller.Load();

  const QString path = QStringLiteral("C:/docs/a.md");
  EXPECT_FALSE(controller.Contains(path));
  EXPECT_TRUE(controller.Toggle(path, QStringLiteral("C:/docs")));
  EXPECT_TRUE(controller.Contains(path));
  ASSERT_EQ(controller.items().size(), 1);
  controller.Save();
  EXPECT_FALSE(settings.bookmarks_json.isEmpty());

  MdViewerSettings reloaded = settings;
  BookmarkController second(&reloaded);
  second.Load();
  EXPECT_TRUE(second.Contains(path));
  EXPECT_FALSE(second.Toggle(path, QStringLiteral("C:/docs")));
  EXPECT_FALSE(second.Contains(path));
}

TEST(BookmarkControllerTest, RemoveDropsEveryMatchingEntry) {
  MdViewerSettings settings;
  BookmarkController controller(&settings);
  controller.Load();
  const QString path = QStringLiteral("C:/docs/a.md");
  controller.Toggle(path, QStringLiteral("C:/docs"));

  controller.Remove(path);
  EXPECT_FALSE(controller.Contains(path));
  EXPECT_TRUE(controller.items().isEmpty());
}
