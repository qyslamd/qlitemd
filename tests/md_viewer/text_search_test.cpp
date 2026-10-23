#include "text_search.h"

#include <atomic>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>

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

TEST(TextSearchTest, FindsLiteralCaseInsensitiveHitsPerLine) {
  QTemporaryDir temp;
  ASSERT_TRUE(temp.isValid());
  const QString path = temp.path() + QStringLiteral("/a.md");
  WriteFile(path, QByteArray("Alpha render\nsecond RENDER render\nnone\r\n"));
  const FileScanResult result =
      ScanFileForQuery(path, QStringLiteral("a.md"), QStringLiteral("render"),
                       kSearchMaxHitsPerFile, nullptr);
  ASSERT_EQ(result.hits.size(), 2);  // 每行一条
  EXPECT_EQ(result.hits[0].line, 1);
  EXPECT_EQ(result.hits[1].line, 2);
  EXPECT_EQ(result.hits[1].file_ordinal, 1);
  // 命中偏移指向源码里的首次出现，大小写按原文保留
  EXPECT_EQ(result.hits[1].snippet.mid(result.hits[1].match_offset, 6),
            QStringLiteral("RENDER"));
}

TEST(TextSearchTest, SnippetKeepsMatchAndBoundsLength) {
  const QString line = QString(300, QLatin1Char('a')) +
                       QStringLiteral("needle") +
                       QString(300, QLatin1Char('b'));
  const SnippetResult snippet = MakeSearchSnippet(line, 300, 6);
  EXPECT_LE(snippet.text.size(), kSearchSnippetLength + 2);
  EXPECT_EQ(snippet.text.mid(snippet.match_offset, 6),
            QStringLiteral("needle"));
}

TEST(TextSearchTest, HandlesLongLineAndCrlf) {
  QTemporaryDir temp;
  ASSERT_TRUE(temp.isValid());
  const QString path = temp.path() + QStringLiteral("/long.md");
  QByteArray bytes(2 * 1024 * 1024, 'x');
  bytes.insert(bytes.size() - 20, " needle");
  bytes.insert(0, "head\r\n");
  WriteFile(path, bytes);
  const FileScanResult result = ScanFileForQuery(
      path, QStringLiteral("long.md"), QStringLiteral("needle"),
      kSearchMaxHitsPerFile, nullptr);
  ASSERT_EQ(result.hits.size(), 1);
  EXPECT_EQ(result.hits[0].line, 2);
  EXPECT_LE(result.hits[0].snippet.size(), kSearchSnippetLength + 2);
  EXPECT_EQ(result.hits[0].snippet.mid(result.hits[0].match_offset, 6),
            QStringLiteral("needle"));
}

TEST(TextSearchTest, HandlesInvalidUtf8) {
  QTemporaryDir temp;
  ASSERT_TRUE(temp.isValid());
  const QString path = temp.path() + QStringLiteral("/bad.md");
  QByteArray bytes =
      QByteArray("ok ") + QByteArray("\xff\xfe", 2) + QByteArray(" needle\n");
  WriteFile(path, bytes);
  const FileScanResult result =
      ScanFileForQuery(path, QStringLiteral("bad.md"), QStringLiteral("needle"),
                       kSearchMaxHitsPerFile, nullptr);
  EXPECT_EQ(result.hits.size(), 1);
}

TEST(TextSearchTest, CapAndCancel) {
  QTemporaryDir temp;
  ASSERT_TRUE(temp.isValid());
  const QString path = temp.path() + QStringLiteral("/many.md");
  QByteArray bytes;
  for (int i = 0; i < 10; ++i) {
    bytes += "needle line\n";
  }
  WriteFile(path, bytes);
  const FileScanResult capped = ScanFileForQuery(
      path, QStringLiteral("many.md"), QStringLiteral("needle"), 3, nullptr);
  EXPECT_EQ(capped.hits.size(), 3);

  std::atomic_bool cancel{true};
  const FileScanResult canceled = ScanFileForQuery(
      path, QStringLiteral("many.md"), QStringLiteral("needle"),
      kSearchMaxHitsPerFile, &cancel);
  EXPECT_TRUE(canceled.canceled);
  EXPECT_TRUE(canceled.hits.isEmpty());
}

TEST(TextSearchTest, CancelsBetweenBatches) {
  QTemporaryDir temp;
  ASSERT_TRUE(temp.isValid());
  for (int f = 0; f < 20; ++f) {
    const QString dir = temp.path() + QStringLiteral("/d%1").arg(f);
    ASSERT_TRUE(QDir().mkpath(dir));
    QByteArray bytes;
    for (int i = 0; i < 200; ++i) {
      bytes += "needle line\n";
    }
    WriteFile(dir + QStringLiteral("/x.md"), bytes);
  }
  std::atomic_bool cancel{false};
  int batches = 0;
  const RootScanResult result =
      ScanRootForQuery(temp.path(), QStringLiteral("needle"), &cancel,
                       [&](const QVector<SearchHit>& hits, int, int) {
                         if (hits.isEmpty()) {
                           return;  // 初始空批只带总数
                         }
                         ++batches;
                         cancel = true;  // 第一批命中后取消
                       });
  EXPECT_TRUE(result.canceled);
  EXPECT_GE(batches, 1);
  EXPECT_GT(result.hits.size(), 0);
  EXPECT_LT(result.hits.size(), 20 * 200);
}

TEST(TextSearchTest, SkipsIgnoredDirsAndTruncatesGlobally) {
  QTemporaryDir temp;
  ASSERT_TRUE(temp.isValid());
  ASSERT_TRUE(QDir(temp.path()).mkpath("node_modules"));
  WriteFile(temp.path() + QStringLiteral("/node_modules/x.md"), "needle\n");
  WriteFile(temp.path() + QStringLiteral("/a.md"), "needle\n");
  const RootScanResult skipped =
      ScanRootForQuery(temp.path(), QStringLiteral("needle"), nullptr, nullptr);
  EXPECT_EQ(skipped.hits.size(), 1);
  EXPECT_EQ(skipped.total_files, 1);

  QTemporaryDir big;
  ASSERT_TRUE(big.isValid());
  for (int f = 0; f < 41; ++f) {
    QByteArray bytes;
    for (int i = 0; i < 50; ++i) {
      bytes += "needle\n";
    }
    WriteFile(big.path() + QStringLiteral("/f%1.md").arg(f), bytes);
  }
  const RootScanResult truncated =
      ScanRootForQuery(big.path(), QStringLiteral("needle"), nullptr, nullptr);
  EXPECT_TRUE(truncated.truncated);
  EXPECT_EQ(truncated.hits.size(), kSearchMaxTotalHits);
}

TEST(TextSearchTest, EmitsBatchesAndFinishes) {
  QTemporaryDir temp;
  ASSERT_TRUE(temp.isValid());
  WriteFile(temp.path() + QStringLiteral("/a.md"), "needle\n");
  TextSearch search;
  QSignalSpy batches(&search, &TextSearch::batchReady);
  QSignalSpy finished(&search, &TextSearch::finished);
  const quint64 request_id =
      search.Start(temp.path(), QStringLiteral("needle"));
  ASSERT_TRUE(finished.wait(30000));
  ASSERT_EQ(finished.count(), 1);
  EXPECT_EQ(finished.at(0).at(0).toULongLong(), request_id);
  EXPECT_FALSE(finished.at(0).at(1).toBool());  // truncated
  EXPECT_FALSE(finished.at(0).at(2).toBool());  // canceled
  EXPECT_GE(batches.count(), 1);
  EXPECT_FALSE(search.IsRunning());
}
