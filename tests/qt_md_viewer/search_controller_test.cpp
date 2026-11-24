#include "search_controller.h"

#include <QCoreApplication>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <gtest/gtest.h>

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

namespace {

void WriteFile(const QString& path, const QByteArray& bytes) {
  QFile file(path);
  ASSERT_TRUE(file.open(QIODevice::WriteOnly));
  ASSERT_EQ(file.write(bytes), bytes.size());
}

}  // namespace

TEST(SearchControllerTest, DocumentFindDebouncesToTheLastQuery) {
  SearchController controller;
  QSignalSpy find_spy(&controller, &SearchController::documentFindRequested);

  controller.ScheduleDocumentFind(QStringLiteral("a"));
  controller.ScheduleDocumentFind(QStringLiteral("ab"));
  EXPECT_EQ(find_spy.count(), 0);

  ASSERT_TRUE(find_spy.wait(2000));
  ASSERT_EQ(find_spy.count(), 1);
  EXPECT_EQ(find_spy.at(0).at(0).toString(), QStringLiteral("ab"));
  EXPECT_FALSE(find_spy.at(0).at(1).toBool());
}

TEST(SearchControllerTest, FindNowSkipsTheDebounce) {
  SearchController controller;
  QSignalSpy find_spy(&controller, &SearchController::documentFindRequested);

  controller.ScheduleDocumentFind(QStringLiteral("a"));
  controller.FindDocumentNow(QStringLiteral("b"), /*backward=*/true);

  ASSERT_EQ(find_spy.count(), 1);
  EXPECT_EQ(find_spy.at(0).at(0).toString(), QStringLiteral("b"));
  EXPECT_TRUE(find_spy.at(0).at(1).toBool());

  QTest::qWait(300);
  EXPECT_EQ(find_spy.count(), 1);
}

TEST(SearchControllerTest, WorkspaceSearchEmitsBatchesThenFinishes) {
  QTemporaryDir temp;
  ASSERT_TRUE(temp.isValid());
  WriteFile(temp.path() + QStringLiteral("/a.md"),
            QByteArray("needle\nNEEDLE\n"));

  SearchController controller;
  QSignalSpy batch_spy(&controller, &SearchController::workspaceSearchBatch);
  QSignalSpy finished_spy(&controller,
                          &SearchController::workspaceSearchFinished);

  controller.SearchWorkspaceNow(temp.path(), QStringLiteral("needle"));
  ASSERT_TRUE(finished_spy.wait(5000));

  EXPECT_GE(batch_spy.count(), 1);
  ASSERT_EQ(finished_spy.count(), 1);
  EXPECT_FALSE(finished_spy.at(0).at(0).toBool());
  EXPECT_FALSE(finished_spy.at(0).at(1).toBool());
  EXPECT_EQ(controller.active_query(), QStringLiteral("needle"));
  EXPECT_FALSE(controller.IsSearching());
}

TEST(SearchControllerTest, ScheduledSearchRunsAfterTheDebounce) {
  QTemporaryDir temp;
  ASSERT_TRUE(temp.isValid());
  WriteFile(temp.path() + QStringLiteral("/a.md"), QByteArray("needle\n"));

  SearchController controller;
  QSignalSpy reset_spy(&controller, &SearchController::workspaceSearchReset);
  QSignalSpy finished_spy(&controller,
                          &SearchController::workspaceSearchFinished);

  controller.ScheduleWorkspaceSearch(temp.path(), QStringLiteral("needle"));
  ASSERT_EQ(reset_spy.count(), 1);
  EXPECT_EQ(reset_spy.at(0).at(0).toString(), QStringLiteral("needle"));

  ASSERT_TRUE(finished_spy.wait(5000));
  ASSERT_EQ(finished_spy.count(), 1);
  EXPECT_FALSE(finished_spy.at(0).at(1).toBool());
}

TEST(SearchControllerTest, ShortQueryResetsWithoutScanning) {
  QTemporaryDir temp;
  ASSERT_TRUE(temp.isValid());

  SearchController controller;
  QSignalSpy reset_spy(&controller, &SearchController::workspaceSearchReset);
  QSignalSpy finished_spy(&controller,
                          &SearchController::workspaceSearchFinished);

  controller.ScheduleWorkspaceSearch(temp.path(), QStringLiteral("a"));
  ASSERT_EQ(reset_spy.count(), 1);
  EXPECT_TRUE(reset_spy.at(0).at(0).toString().isEmpty());

  QTest::qWait(500);
  EXPECT_EQ(finished_spy.count(), 0);
}

TEST(SearchControllerTest, ResetCancelsThePendingDebounce) {
  QTemporaryDir temp;
  ASSERT_TRUE(temp.isValid());

  SearchController controller;
  QSignalSpy finished_spy(&controller,
                          &SearchController::workspaceSearchFinished);

  controller.ScheduleWorkspaceSearch(temp.path(), QStringLiteral("needle"));
  controller.ResetWorkspaceSearch();

  QTest::qWait(500);
  EXPECT_EQ(finished_spy.count(), 0);
  EXPECT_TRUE(controller.active_query().isEmpty());
  EXPECT_FALSE(controller.IsSearching());
}

TEST(SearchControllerTest, MissingRootReportsFailure) {
  SearchController controller;
  QSignalSpy failed_spy(&controller, &SearchController::workspaceSearchFailed);

  controller.SearchWorkspaceNow(QString(), QStringLiteral("needle"));

  ASSERT_EQ(failed_spy.count(), 1);
  EXPECT_EQ(failed_spy.at(0).at(0).toString(),
            QStringLiteral("先打开一个目录"));
}
