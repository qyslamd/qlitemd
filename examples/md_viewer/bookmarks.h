#ifndef FILE_EXAMPLES_MD_VIEWER_BOOKMARKS_H_
#define FILE_EXAMPLES_MD_VIEWER_BOOKMARKS_H_

#include <QString>
#include <QVector>

struct Bookmark {
  QString path;  // 绝对路径
  QString root;  // 星标时的根目录

  bool operator==(const Bookmark& other) const {
    return path == other.path && root == other.root;
  }
};

// 文件级书签；顺序即展示顺序（新收藏在前）。持久化格式是 JSON 数组。
class BookmarkStore {
 public:
  static BookmarkStore FromJson(const QString& json);
  QString ToJson() const;
  bool Contains(const QString& path) const;
  // true=现在已收藏，false=已移除
  bool Toggle(const QString& path, const QString& root);
  void Remove(const QString& path);
  const QVector<Bookmark>& items() const;

 private:
  QVector<Bookmark> items_;
};

struct ReadingPosition {
  QString path;
  int scroll = 0;
  qint64 touched = 0;
};

// 按绝对路径记住阅读位置，最多 capacity 条，LRU 淘汰。
class ReadingPositionStore {
 public:
  static ReadingPositionStore FromJson(const QString& json, int capacity = 500);
  QString ToJson() const;
  // 不存在返回 -1
  int ScrollFor(const QString& path) const;
  void Update(const QString& path, int scroll, qint64 now_seconds);
  int size() const;

 private:
  QVector<ReadingPosition> items_;
  int capacity_ = 500;
};

// 当前根优先，其次星标时的根，最后绝对路径；分隔符统一为 '/'
QString DisplayBookmarkPath(const Bookmark& bookmark,
                            const QString& current_root);

#endif  // FILE_EXAMPLES_MD_VIEWER_BOOKMARKS_H_
