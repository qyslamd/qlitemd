#include "bookmarks.h"

#include <QDir>

#include <gtest/gtest.h>

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

TEST(ReadingPositionStoreTest, EvictsLeastRecentlyUsed) {
  ReadingPositionStore store = ReadingPositionStore::FromJson(QString(), 3);
  store.Update(QStringLiteral("a"), 10, 1);
  store.Update(QStringLiteral("b"), 20, 2);
  store.Update(QStringLiteral("c"), 30, 3);
  store.Update(QStringLiteral("b"), 21, 4);
  store.Update(QStringLiteral("d"), 40, 5);  // a 应被淘汰
  EXPECT_EQ(store.ScrollFor(QStringLiteral("a")), -1);
  EXPECT_EQ(store.ScrollFor(QStringLiteral("b")), 21);
  EXPECT_EQ(store.size(), 3);
  const ReadingPositionStore loaded =
      ReadingPositionStore::FromJson(store.ToJson(), 3);
  EXPECT_EQ(loaded.ScrollFor(QStringLiteral("d")), 40);
}
