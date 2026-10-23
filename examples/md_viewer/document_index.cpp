#include "document_index.h"

#include <algorithm>

#include <QDir>
#include <QFileInfo>
#include <QFileInfoList>
#include <QSet>

namespace {

bool LessByRelativePath(const DocumentEntry& a, const DocumentEntry& b) {
  return a.relative_path.compare(b.relative_path, Qt::CaseInsensitive) < 0;
}

QString RelativePath(const QString& root, const QString& absolute_path) {
  return QDir::fromNativeSeparators(QDir(root).relativeFilePath(absolute_path));
}

// 把相对目录及其所有祖先收进集合，避免为空目录建树节点
void AddDirectoryAndAncestors(const QString& file_relative_path,
                              QSet<QString>* directories) {
  QString directory = QFileInfo(file_relative_path).path();
  while (!directory.isEmpty() && directory != QStringLiteral(".")) {
    directories->insert(directory);
    directory = QFileInfo(directory).path();
  }
}

void CollectMarkdownEntries(const QString& root, const QString& directory,
                            QVector<DocumentEntry>* files,
                            QSet<QString>* directories,
                            QStringList* watch_directories) {
  QDir dir(directory);
  dir.setFilter(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot);
  const QFileInfoList entries = dir.entryInfoList();
  for (const QFileInfo& info : entries) {
    // 不跟随符号链接：目录环和跳出去的链接都不在阅读范围里
    if (info.isSymLink()) {
      continue;
    }
    if (info.isDir()) {
      if (!IsIgnoredDirectoryName(info.fileName())) {
        watch_directories->append(info.absoluteFilePath());
        CollectMarkdownEntries(root, info.absoluteFilePath(), files,
                               directories, watch_directories);
      }
      continue;
    }
    if (!IsMarkdownDocumentName(info.fileName())) {
      continue;
    }
    DocumentEntry entry;
    entry.absolute_path = info.absoluteFilePath();
    entry.relative_path = RelativePath(root, entry.absolute_path);
    entry.size = info.size();
    entry.modified = info.lastModified();
    entry.is_dir = false;
    files->append(entry);
    AddDirectoryAndAncestors(entry.relative_path, directories);
  }
}

}  // namespace

bool IsMarkdownDocumentName(const QString& file_name) {
  const QString suffix = QFileInfo(file_name).suffix();
  return suffix.compare(QStringLiteral("md"), Qt::CaseInsensitive) == 0 ||
         suffix.compare(QStringLiteral("markdown"), Qt::CaseInsensitive) == 0;
}

bool IsIgnoredDirectoryName(const QString& directory_name) {
  return directory_name.startsWith(QLatin1Char('.')) ||
         directory_name.compare(QStringLiteral("node_modules"),
                                Qt::CaseInsensitive) == 0 ||
         directory_name.startsWith(QStringLiteral("build"),
                                   Qt::CaseInsensitive);
}

DocumentScanResult ScanDocumentRoot(const QString& root) {
  DocumentScanResult result;
  const QFileInfo root_info(root);
  result.root = QDir::cleanPath(root_info.absoluteFilePath());
  if (!root_info.exists() || !root_info.isDir() || !root_info.isReadable()) {
    result.root_exists = false;
    return result;
  }

  QVector<DocumentEntry> files;
  QSet<QString> directories;
  CollectMarkdownEntries(result.root, result.root, &files, &directories,
                         &result.watch_directories);
  std::sort(result.watch_directories.begin(), result.watch_directories.end(),
            [](const QString& a, const QString& b) {
              return a.compare(b, Qt::CaseInsensitive) < 0;
            });

  std::sort(files.begin(), files.end(), LessByRelativePath);

  QVector<DocumentEntry> dir_entries;
  dir_entries.reserve(directories.size());
  for (const QString& relative_path : directories) {
    DocumentEntry entry;
    entry.absolute_path = QDir(result.root).absoluteFilePath(relative_path);
    entry.relative_path = relative_path;
    entry.modified = QFileInfo(entry.absolute_path).lastModified();
    entry.is_dir = true;
    dir_entries.append(entry);
  }
  std::sort(dir_entries.begin(), dir_entries.end(), LessByRelativePath);

  result.markdown_file_count = files.size();
  result.entries = dir_entries;
  result.entries.append(files);
  return result;
}

QStringList CollectMarkdownFiles(const QString& root) {
  QStringList files;
  const DocumentScanResult result = ScanDocumentRoot(root);
  for (const DocumentEntry& entry : result.entries) {
    if (!entry.is_dir) {
      files.append(entry.absolute_path);
    }
  }
  return files;
}

bool IsPathUnderRoot(const QString& path, const QString& root) {
  if (root.isEmpty()) {
    return false;
  }
  QString normalized_root = QDir::cleanPath(QFileInfo(root).absoluteFilePath());
  QString normalized_path = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
  if (!normalized_root.endsWith(QLatin1Char('/'))) {
    normalized_root += QLatin1Char('/');
  }
  if (!normalized_path.endsWith(QLatin1Char('/'))) {
    normalized_path += QLatin1Char('/');
  }
#ifdef Q_OS_WIN
  return normalized_path.startsWith(normalized_root, Qt::CaseInsensitive);
#else
  return normalized_path.startsWith(normalized_root, Qt::CaseSensitive);
#endif
}

QStringList DirectoriesToWatch(const DocumentScanResult& result,
                               int max_directories) {
  if (result.watch_directories.size() > max_directories) {
    return {result.root};
  }
  QStringList directories{result.root};
  directories.append(result.watch_directories);
  return directories;
}

QString DocumentDisplayTitle(const QString& root, const QString& path) {
  const QString file_name = QFileInfo(path).fileName();
  if (root.isEmpty() || !IsPathUnderRoot(path, root)) {
    return file_name;
  }
  const QString relative = QDir(root).relativeFilePath(path);
  if (relative.isEmpty() || relative == QStringLiteral(".")) {
    return file_name;
  }
  const QString root_name = QFileInfo(root).fileName();
  if (root_name.isEmpty()) {
    return relative;
  }
  return root_name + QStringLiteral(" / ") + relative;
}
