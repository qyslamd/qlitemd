#include "reading_positions.h"

#include <gtest/gtest.h>

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
