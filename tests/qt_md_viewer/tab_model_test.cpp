#include "tab_model.h"

#include <gtest/gtest.h>

TEST(TabModelTest, StartsWithWelcomeTab) {
  TabModel model;
  EXPECT_EQ(model.count(), 1);
  EXPECT_EQ(model.active(), 0);
  EXPECT_TRUE(model.path_at(0).isEmpty());
}

TEST(TabModelTest, AddAppendsAndReturnsIndex) {
  TabModel model;
  EXPECT_EQ(model.Add(QStringLiteral("/a")), 1);
  EXPECT_EQ(model.Add(QStringLiteral("/b")), 2);
  EXPECT_EQ(model.count(), 3);
  EXPECT_EQ(model.path_at(1), QStringLiteral("/a"));
  EXPECT_EQ(model.path_at(2), QStringLiteral("/b"));
}

TEST(TabModelTest, AddDuplicateReturnsExistingIndex) {
  TabModel model;
  const int first = model.Add(QStringLiteral("/a"));
  EXPECT_EQ(model.Add(QStringLiteral("/a")), first);
  EXPECT_EQ(model.count(), 2);
}

TEST(TabModelTest, AddEmptyReturnsWelcome) {
  TabModel model;
  EXPECT_EQ(model.Add(QString()), 0);
  EXPECT_EQ(model.count(), 1);
}

TEST(TabModelTest, RemoveWelcomeIsNoop) {
  TabModel model;
  model.Add(QStringLiteral("/a"));
  EXPECT_FALSE(model.Remove(0));
  EXPECT_EQ(model.count(), 2);
  EXPECT_TRUE(model.path_at(0).isEmpty());
}

TEST(TabModelTest, RemoveActiveActivatesNextTab) {
  TabModel model;
  model.Add(QStringLiteral("/a"));
  model.Add(QStringLiteral("/b"));
  model.SetActive(1);
  EXPECT_TRUE(model.Remove(1));
  EXPECT_EQ(model.count(), 2);
  EXPECT_EQ(model.active(), 1);
  EXPECT_EQ(model.path_at(model.active()), QStringLiteral("/b"));
}

TEST(TabModelTest, RemoveLastActiveFallsBackToPrevious) {
  TabModel model;
  model.Add(QStringLiteral("/a"));
  model.Add(QStringLiteral("/b"));
  model.SetActive(2);
  EXPECT_TRUE(model.Remove(2));
  EXPECT_EQ(model.active(), 1);
  EXPECT_EQ(model.path_at(model.active()), QStringLiteral("/a"));
}

TEST(TabModelTest, RemoveBeforeActiveKeepsSameDocumentActive) {
  TabModel model;
  model.Add(QStringLiteral("/a"));
  model.Add(QStringLiteral("/b"));
  model.Add(QStringLiteral("/c"));
  model.SetActive(3);
  EXPECT_TRUE(model.Remove(1));
  EXPECT_EQ(model.active(), 2);
  EXPECT_EQ(model.path_at(model.active()), QStringLiteral("/c"));
}

TEST(TabModelTest, MoveKeepsActiveOnSameDocument) {
  TabModel model;
  model.Add(QStringLiteral("/a"));
  model.Add(QStringLiteral("/b"));
  model.Add(QStringLiteral("/c"));
  model.SetActive(1);
  EXPECT_TRUE(model.Move(1, 3));
  EXPECT_EQ(model.path_at(3), QStringLiteral("/a"));
  EXPECT_EQ(model.active(), 3);
}

TEST(TabModelTest, MoveRejectsWelcomeAndIntoWelcome) {
  TabModel model;
  model.Add(QStringLiteral("/a"));
  model.Add(QStringLiteral("/b"));
  EXPECT_FALSE(model.Move(0, 2));
  EXPECT_FALSE(model.Move(1, 0));
  EXPECT_EQ(model.path_at(0), QString());
  EXPECT_EQ(model.path_at(1), QStringLiteral("/a"));
  EXPECT_EQ(model.path_at(2), QStringLiteral("/b"));
}

TEST(TabModelTest, SerializeRestoreRoundTrip) {
  TabModel model;
  model.Add(QStringLiteral("/a"));
  model.Add(QStringLiteral("/b"));
  model.SetActive(2);
  const QByteArray data = model.Serialize();

  TabModel restored;
  EXPECT_TRUE(restored.Restore(data));
  EXPECT_EQ(restored.count(), 3);
  EXPECT_EQ(restored.active(), 2);
  EXPECT_EQ(restored.path_at(0), QString());
  EXPECT_EQ(restored.path_at(1), QStringLiteral("/a"));
  EXPECT_EQ(restored.path_at(2), QStringLiteral("/b"));
}

TEST(TabModelTest, RestoreRejectsInvalidJson) {
  TabModel model;
  model.Add(QStringLiteral("/a"));
  EXPECT_FALSE(model.Restore(QByteArrayLiteral("{not json")));
  EXPECT_EQ(model.count(), 2);
  EXPECT_EQ(model.path_at(1), QStringLiteral("/a"));
}

TEST(TabModelTest, RestoreDedupesAndClampsActive) {
  TabModel restored;
  const QByteArray data = QByteArrayLiteral(
      "{\"tabs\":[\"\",\"/a\",\"/a\",\"\",\"/b\"],"
      "\"active\":99}");
  EXPECT_TRUE(restored.Restore(data));
  EXPECT_EQ(restored.count(), 3);
  EXPECT_EQ(restored.path_at(1), QStringLiteral("/a"));
  EXPECT_EQ(restored.path_at(2), QStringLiteral("/b"));
  EXPECT_EQ(restored.active(), 2);
}
