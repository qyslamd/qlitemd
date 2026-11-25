#include "bookmarks.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QStandardPaths>

#include <gtest/gtest.h>

#include "md_viewer_settings.h"

int main(int argc, char** argv) {
  QStandardPaths::setTestModeEnabled(true);
  QCoreApplication::setOrganizationName(QStringLiteral("md-tests"));
  QCoreApplication::setApplicationName(
      QStringLiteral("bookmarks-%1").arg(QDateTime::currentMSecsSinceEpoch()));
  QCoreApplication app(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

#include <QDir>

TEST(BookmarkStoreTest, TogglesAndRoundTrips) {
  BookmarkStore store;
  EXPECT_TRUE(store.Toggle(QStringLiteral("C:/docs/甲 文档.md"),
                           QStringLiteral("C:/docs")));
  EXPECT_TRUE(store.Contains(QStringLiteral("C:/docs/甲 文档.md")));
  EXPECT_FALSE(store.Toggle(QStringLiteral("C:/docs/甲 文档.md"),
                            QStringLiteral("C:/docs")));
  EXPECT_FALSE(store.Contains(QStringLiteral("C:/docs/甲 文档.md")));

  store.Toggle(QStringLiteral("C:/docs/a.md"), QStringLiteral("C:/docs"));
  store.Toggle(QStringLiteral("C:/docs/b.md"), QStringLiteral("C:/docs"));
  const BookmarkStore loaded = BookmarkStore::FromJson(store.ToJson());
  ASSERT_EQ(loaded.items().size(), 2);
  EXPECT_EQ(loaded.items()[0].path, QStringLiteral("C:/docs/b.md"));
  EXPECT_EQ(loaded.items()[1].path, QStringLiteral("C:/docs/a.md"));
  EXPECT_EQ(BookmarkStore::FromJson(QStringLiteral("not json")).items().size(),
            0);
}

TEST(BookmarkStoreTest, DisplaysRelativeToCurrentRootFirst) {
  const QString root = QDir::tempPath() + QStringLiteral("/qlitemd-docs");
  const QString guide = root + QStringLiteral("/guide");
  const Bookmark bookmark{guide + QStringLiteral("/a.md"), root};
  EXPECT_EQ(DisplayBookmarkPath(bookmark, guide), QStringLiteral("a.md"));
  EXPECT_EQ(DisplayBookmarkPath(bookmark,
                                QDir::tempPath() + QStringLiteral("/other")),
            QStringLiteral("guide/a.md"));

  const QString other = QDir::tempPath() + QStringLiteral("/other");
  const QString outside_path = other + QStringLiteral("/b.md");
  const Bookmark under_star_root{outside_path, other};
  EXPECT_EQ(DisplayBookmarkPath(under_star_root, root), QStringLiteral("b.md"));
  // 两支根都不包含它时退回绝对路径
  const Bookmark no_matching_root{outside_path,
                                  QDir::tempPath() + QStringLiteral("/star")};
  EXPECT_EQ(DisplayBookmarkPath(no_matching_root, root),
            QDir::fromNativeSeparators(outside_path));
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
