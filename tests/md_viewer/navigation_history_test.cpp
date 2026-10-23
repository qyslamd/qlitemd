#include "navigation_history.h"

#include <gtest/gtest.h>

TEST(NavigationHistoryTest, WalksBackAndForward) {
  NavigationHistory history;
  history.Push({QStringLiteral("/a"), QString()});
  history.Push({QStringLiteral("/b"), QStringLiteral("x")});
  history.Push({QStringLiteral("/c"), QString()});
  ASSERT_EQ(history.size(), 3);
  EXPECT_TRUE(history.CanGoBack());
  const std::optional<NavigationEntry> b = history.Back();
  ASSERT_TRUE(b.has_value());
  EXPECT_EQ(b->path, QStringLiteral("/b"));
  EXPECT_EQ(b->fragment, QStringLiteral("x"));
  const std::optional<NavigationEntry> a = history.Back();
  ASSERT_TRUE(a.has_value());
  EXPECT_EQ(a->path, QStringLiteral("/a"));
  EXPECT_FALSE(history.Back().has_value());
  EXPECT_TRUE(history.CanGoForward());
  EXPECT_EQ(history.Forward()->path, QStringLiteral("/b"));
}

TEST(NavigationHistoryTest, PushAfterBackDropsForwardEntries) {
  NavigationHistory history;
  history.Push({QStringLiteral("/a"), QString()});
  history.Push({QStringLiteral("/b"), QString()});
  history.Push({QStringLiteral("/c"), QString()});
  history.Back();
  history.Push({QStringLiteral("/d"), QString()});
  EXPECT_FALSE(history.CanGoForward());
  EXPECT_EQ(history.size(), 3);
}

TEST(NavigationHistoryTest, ConsecutiveDuplicateIsIgnored) {
  NavigationHistory history;
  history.Push({QStringLiteral("/a"), QString()});
  history.Push({QStringLiteral("/a"), QString()});
  EXPECT_EQ(history.size(), 1);
}
