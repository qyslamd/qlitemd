#include "workspace_model.h"

#include <iostream>

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <gtest/gtest.h>

namespace {

void WriteFile(const QString& path, const QString& text) {
  QDir().mkpath(QFileInfo(path).absolutePath());
  QFile file(path);
  ASSERT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Text));
  file.write(text.toUtf8());
}

}  // namespace

int main(int argc, char** argv) {
  QStandardPaths::setTestModeEnabled(true);
  QCoreApplication::setOrganizationName(QStringLiteral("md-tests"));
  QCoreApplication::setApplicationName(
      QStringLiteral("workspace-%1").arg(QDateTime::currentMSecsSinceEpoch()));
  QCoreApplication app(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

TEST(WorkspaceModelTest, ScansGroupsAndSkipsIgnoredDirectories) {
  QTemporaryDir dir;
  ASSERT_TRUE(dir.isValid());
  const QString root = dir.path();
  WriteFile(root + QStringLiteral("/README.md"), QStringLiteral("# root"));
  WriteFile(root + QStringLiteral("/notes/topic.md"),
            QStringLiteral("# topic"));
  WriteFile(root + QStringLiteral("/notes/sub/deep.markdown"),
            QStringLiteral("# deep"));
  WriteFile(root + QStringLiteral("/.git/ignored.md"), QStringLiteral("# no"));
  WriteFile(root + QStringLiteral("/build/ignored.md"), QStringLiteral("# no"));
  WriteFile(root + QStringLiteral("/notes/skip.txt"),
            QStringLiteral("not markdown"));

  WorkspaceModel model;
  ASSERT_TRUE(model.Load(root));
  ASSERT_EQ(model.count(), 3);
  EXPECT_EQ(model.documents()[0].group, QStringLiteral("ROOT"));
  EXPECT_EQ(model.documents()[0].relative_path, QStringLiteral("README.md"));
  EXPECT_EQ(model.documents()[1].group, QStringLiteral("notes"));
  EXPECT_EQ(model.documents()[1].relative_path,
            QStringLiteral("notes/sub/deep.markdown"));
  EXPECT_EQ(model.documents()[2].title, QStringLiteral("topic"));
  EXPECT_EQ(model.indexOf(root + QStringLiteral("/notes/topic.md")), 2);
  EXPECT_EQ(model.indexOf(root + QStringLiteral("/nope.md")), -1);
}

TEST(WorkspaceModelTest, PersistsAndRestoresPositions) {
  QTemporaryDir dir;
  ASSERT_TRUE(dir.isValid());
  WorkspaceModel model;
  ASSERT_TRUE(model.Load(dir.path()));
  WriteFile(dir.path() + QStringLiteral("/a.md"), QStringLiteral("# a"));
  ASSERT_TRUE(model.Load(dir.path()));
  ASSERT_EQ(model.count(), 1);

  model.SetPosition(0, 120, 50, true);
  EXPECT_EQ(model.overallProgress(), 50);
  model.Flush();

  WorkspaceModel restored;
  ASSERT_TRUE(restored.Load(dir.path()));
  ASSERT_EQ(restored.count(), 1);
  EXPECT_EQ(restored.documents()[0].scroll, 120);
  EXPECT_EQ(restored.documents()[0].progress, 50);
}

TEST(WorkspaceModelTest, LoadsTwoHundredDocumentsQuickly) {
  QTemporaryDir dir;
  ASSERT_TRUE(dir.isValid());
  for (int i = 0; i < 200; ++i) {
    WriteFile(
        dir.path() + QStringLiteral("/group%1/doc%2.md").arg(i % 10).arg(i),
        QStringLiteral("# %1\n\nbody\n").arg(i));
  }
  WorkspaceModel model;
  QElapsedTimer timer;
  timer.start();
  ASSERT_TRUE(model.Load(dir.path()));
  const qint64 elapsed_ms = timer.elapsed();
  EXPECT_EQ(model.count(), 200);
  EXPECT_LT(elapsed_ms, 2000);
  std::cout << "workspace load 200 docs: " << elapsed_ms << " ms\n";
}

TEST(WorkspaceModelTest, ClampsPositionValues) {
  QTemporaryDir dir;
  ASSERT_TRUE(dir.isValid());
  WriteFile(dir.path() + QStringLiteral("/a.md"), QStringLiteral("# a"));
  WorkspaceModel model;
  ASSERT_TRUE(model.Load(dir.path()));
  model.SetPosition(0, -4, 180, false);
  EXPECT_EQ(model.documents()[0].scroll, 0);
  EXPECT_EQ(model.documents()[0].progress, 100);
  model.SetPosition(9, 10, 10, false);
  EXPECT_EQ(model.count(), 1);
}
