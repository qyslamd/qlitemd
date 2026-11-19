#include "welcome_view.h"

#include <QApplication>
#include <QFile>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSignalSpy>
#include <QTemporaryDir>

#include <gtest/gtest.h>

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QApplication app(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

namespace {

QString MakeFile(const QTemporaryDir& dir, const QString& name) {
  const QString path = dir.filePath(name);
  QFile file(path);
  file.open(QIODevice::WriteOnly | QIODevice::Text);
  file.write("# doc\n");
  return path;
}

}  // namespace

TEST(WelcomeViewTest, FiltersMissingFilesAndKeepsLatestFive) {
  QTemporaryDir dir;
  ASSERT_TRUE(dir.isValid());
  QStringList paths;
  for (int i = 0; i < 6; ++i) {
    paths.append(MakeFile(dir, QStringLiteral("doc%1.md").arg(i)));
  }
  paths.prepend(dir.filePath(QStringLiteral("missing.md")));

  WelcomeView view;
  view.SetRecentFiles(paths, dir.path());
  auto* list =
      view.findChild<QListWidget*>(QStringLiteral("welcomeRecentList"));
  ASSERT_NE(list, nullptr);
  EXPECT_EQ(list->count(), 5);
  EXPECT_EQ(list->item(0)->data(Qt::UserRole).toString(),
            dir.filePath(QStringLiteral("doc0.md")));
  EXPECT_EQ(list->item(4)->data(Qt::UserRole).toString(),
            dir.filePath(QStringLiteral("doc4.md")));
}

TEST(WelcomeViewTest, EmptyRecentShowsHint) {
  WelcomeView view;
  view.SetRecentFiles({}, QString());
  auto* list =
      view.findChild<QListWidget*>(QStringLiteral("welcomeRecentList"));
  auto* hint = view.findChild<QLabel*>(QStringLiteral("welcomeEmptyHint"));
  ASSERT_NE(list, nullptr);
  ASSERT_NE(hint, nullptr);
  EXPECT_TRUE(list->isHidden());
  EXPECT_FALSE(hint->isHidden());
}

TEST(WelcomeViewTest, ActionsAndRecentEmit) {
  QTemporaryDir dir;
  ASSERT_TRUE(dir.isValid());
  const QString path = MakeFile(dir, QStringLiteral("recent.md"));

  WelcomeView view;
  view.SetRecentFiles({path}, dir.path());
  auto* list =
      view.findChild<QListWidget*>(QStringLiteral("welcomeRecentList"));
  ASSERT_NE(list, nullptr);
  ASSERT_EQ(list->count(), 1);

  QSignalSpy recent_spy(&view, &WelcomeView::recentActivated);
  emit list->itemClicked(list->item(0));
  ASSERT_EQ(recent_spy.count(), 1);
  EXPECT_EQ(recent_spy.takeFirst().at(0).toString(), path);

  auto* open_dir =
      view.findChild<QPushButton*>(QStringLiteral("welcomeOpenDirectory"));
  auto* open_file =
      view.findChild<QPushButton*>(QStringLiteral("welcomeOpenFile"));
  ASSERT_NE(open_dir, nullptr);
  ASSERT_NE(open_file, nullptr);
  QSignalSpy directory_spy(&view, &WelcomeView::openDirectoryRequested);
  QSignalSpy file_spy(&view, &WelcomeView::openFileRequested);
  open_dir->click();
  open_file->click();
  EXPECT_EQ(directory_spy.count(), 1);
  EXPECT_EQ(file_spy.count(), 1);
}
