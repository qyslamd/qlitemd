#include "document_index.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <gtest/gtest.h>

namespace {

void WriteFile(const QString& path, const QByteArray& bytes) {
  QFile file(path);
  ASSERT_TRUE(file.open(QIODevice::WriteOnly));
  ASSERT_EQ(file.write(bytes), bytes.size());
}

}  // namespace

TEST(DocumentIndexTest, ScansNestedDocsAndSkipsIgnoredDirs) {
  QTemporaryDir temp;
  ASSERT_TRUE(temp.isValid());
  const QString root = temp.path();
  ASSERT_TRUE(QDir(root).mkpath("guide/deep"));
  ASSERT_TRUE(QDir(root).mkpath("empty"));
  ASSERT_TRUE(QDir(root).mkpath(".hidden"));
  ASSERT_TRUE(QDir(root).mkpath("node_modules"));
  ASSERT_TRUE(QDir(root).mkpath("build-out"));
  WriteFile(root + "/a.md", "a");
  WriteFile(root + "/guide/b.markdown", "bb");
  WriteFile(root + "/guide/deep/中文 文档.md", "ccc");
  WriteFile(root + "/notes.txt", "n");
  WriteFile(root + "/empty/readme.txt", "n");
  WriteFile(root + "/.hidden/x.md", "x");
  WriteFile(root + "/node_modules/y.md", "y");
  WriteFile(root + "/build-out/z.md", "z");

  const DocumentScanResult result = ScanDocumentRoot(root);
  ASSERT_TRUE(result.root_exists);
  ASSERT_EQ(result.markdown_file_count, 3);
  ASSERT_EQ(result.entries.size(), 5);  // 2 个目录 + 3 个文件
  EXPECT_TRUE(result.entries[0].is_dir);
  EXPECT_EQ(result.entries[0].relative_path, QStringLiteral("guide"));
  EXPECT_TRUE(result.entries[1].is_dir);
  EXPECT_EQ(result.entries[1].relative_path, QStringLiteral("guide/deep"));
  EXPECT_EQ(result.entries[2].relative_path, QStringLiteral("a.md"));
  EXPECT_EQ(result.entries[3].relative_path,
            QStringLiteral("guide/b.markdown"));
  EXPECT_EQ(result.entries[4].relative_path,
            QStringLiteral("guide/deep/中文 文档.md"));
  EXPECT_EQ(result.entries[4].size, 3);
  EXPECT_EQ(CollectMarkdownFiles(root).size(), 3);
  // 空目录也要监听：往里新建 md 时树才会刷新
  EXPECT_TRUE(
      result.watch_directories.contains(root + QStringLiteral("/empty")));
  EXPECT_FALSE(
      result.watch_directories.contains(root + QStringLiteral("/.hidden")));
  EXPECT_FALSE(result.watch_directories.contains(
      root + QStringLiteral("/node_modules")));
  EXPECT_FALSE(
      result.watch_directories.contains(root + QStringLiteral("/build-out")));
}

TEST(DocumentIndexTest, MissingRootReportsNotFound) {
  QTemporaryDir temp;
  ASSERT_TRUE(temp.isValid());
  const DocumentScanResult result =
      ScanDocumentRoot(temp.path() + QStringLiteral("/missing-root"));
  EXPECT_FALSE(result.root_exists);
  EXPECT_TRUE(result.entries.isEmpty());
  EXPECT_EQ(result.markdown_file_count, 0);
}

TEST(DocumentIndexTest, MatchesIgnoreRules) {
  EXPECT_TRUE(IsIgnoredDirectoryName(QStringLiteral(".git")));
  EXPECT_TRUE(IsIgnoredDirectoryName(QStringLiteral("node_modules")));
  EXPECT_TRUE(IsIgnoredDirectoryName(QStringLiteral("Node_Modules")));
  EXPECT_TRUE(IsIgnoredDirectoryName(QStringLiteral("build")));
  EXPECT_TRUE(IsIgnoredDirectoryName(QStringLiteral("build_qt515")));
  EXPECT_FALSE(IsIgnoredDirectoryName(QStringLiteral("docs")));
  EXPECT_TRUE(IsMarkdownDocumentName(QStringLiteral("A.MD")));
  EXPECT_TRUE(IsMarkdownDocumentName(QStringLiteral("a.markdown")));
  EXPECT_FALSE(IsMarkdownDocumentName(QStringLiteral("a.txt")));
}

TEST(DocumentIndexTest, PathsRespectRootBoundary) {
  const QString root = QDir::tempPath() + QStringLiteral("/qlitemd-root");
  EXPECT_TRUE(IsPathUnderRoot(root + QStringLiteral("/a.md"), root));
  EXPECT_TRUE(IsPathUnderRoot(root, root));
  EXPECT_FALSE(IsPathUnderRoot(root + QStringLiteral("/../outside.md"), root));
  EXPECT_FALSE(IsPathUnderRoot(root + QStringLiteral("2/a.md"), root));
}

TEST(DocumentIndexTest, DisplayTitleShowsRootAndRelativePath) {
  const QString root = QDir::tempPath() + QStringLiteral("/qlitemd-root");
  EXPECT_EQ(DocumentDisplayTitle(root, root + QStringLiteral("/guide/a.md")),
            QStringLiteral("qlitemd-root / guide/a.md"));
  EXPECT_EQ(DocumentDisplayTitle(root, root + QStringLiteral("/a.md")),
            QStringLiteral("qlitemd-root / a.md"));
}

TEST(DocumentIndexTest, DisplayTitleFallsBackToFileNameOutsideRoot) {
  const QString root = QDir::tempPath() + QStringLiteral("/qlitemd-root");
  EXPECT_EQ(
      DocumentDisplayTitle(root, QDir::tempPath() + QStringLiteral("/b.md")),
      QStringLiteral("b.md"));
  EXPECT_EQ(
      DocumentDisplayTitle(QString(), root + QStringLiteral("/guide/a.md")),
      QStringLiteral("a.md"));
}

TEST(DocumentIndexTest, WatcherFallsBackToRootBeyondLimit) {
  DocumentScanResult result;
  result.root = QStringLiteral("/docs");
  for (int i = 0; i < 201; ++i) {
    result.watch_directories.append(QStringLiteral("/docs/d%1").arg(i));
  }
  EXPECT_EQ(DirectoriesToWatch(result), QStringList{result.root});
}

TEST(DocumentIndexTest, ScansManyDirectories) {
  QTemporaryDir temp;
  ASSERT_TRUE(temp.isValid());
  for (int i = 0; i < 205; ++i) {
    const QString dir = temp.path() + QStringLiteral("/d%1").arg(i);
    ASSERT_TRUE(QDir().mkpath(dir));
    WriteFile(dir + QStringLiteral("/x.md"), "x");
  }
  const DocumentScanResult result = ScanDocumentRoot(temp.path());
  EXPECT_TRUE(result.root_exists);
  EXPECT_EQ(result.markdown_file_count, 205);
}
