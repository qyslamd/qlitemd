#ifndef FILE_EXAMPLES_MD_VIEWER_DOCUMENT_INDEX_H_
#define FILE_EXAMPLES_MD_VIEWER_DOCUMENT_INDEX_H_

#include <QDateTime>
#include <QString>
#include <QStringList>
#include <QVector>

struct DocumentEntry {
  QString absolute_path;
  QString relative_path;  // 统一使用 '/'
  qint64 size = 0;
  QDateTime modified;
  bool is_dir = false;
};

struct DocumentScanResult {
  QString root;  // 绝对路径
  // 先目录、后文件，各自按 relative_path 不区分大小写排序
  QVector<DocumentEntry> entries;
  int markdown_file_count = 0;
  // 根不存在、不是目录或不可读时为 false
  bool root_exists = true;
  // 所有未被忽略的子目录（含空目录）；watcher 要监听它们，
  // 否则往空目录里新建 md 不会触发刷新
  QStringList watch_directories;
};

bool IsMarkdownDocumentName(const QString& file_name);
bool IsIgnoredDirectoryName(const QString& directory_name);
DocumentScanResult ScanDocumentRoot(const QString& root);
// 绝对路径，按 relative_path 排序
QStringList CollectMarkdownFiles(const QString& root);
bool IsPathUnderRoot(const QString& path, const QString& root);
QStringList DirectoriesToWatch(const DocumentScanResult& result,
                               int max_directories = 200);
// 标题栏用：根目录名 + 相对路径；文件在根外时退回文件名
QString DocumentDisplayTitle(const QString& root, const QString& path);

#endif  // FILE_EXAMPLES_MD_VIEWER_DOCUMENT_INDEX_H_
