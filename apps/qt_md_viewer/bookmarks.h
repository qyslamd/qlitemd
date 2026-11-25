#ifndef FILE_APPS_QT_MD_VIEWER_BOOKMARKS_H_
#define FILE_APPS_QT_MD_VIEWER_BOOKMARKS_H_

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

// 当前根优先，其次星标时的根，最后绝对路径；分隔符统一为 '/'
QString DisplayBookmarkPath(const Bookmark& bookmark,
                            const QString& current_root);

struct MdViewerSettings;

// 星标集合 + bookmarks_json 的读写。星标按钮、侧栏列表等 UI 同步
// 由宿主负责。
class BookmarkController {
 public:
  explicit BookmarkController(MdViewerSettings* settings);

  // 从 settings_->bookmarks_json 读入
  void Load();
  bool Contains(const QString& path) const;
  // 返回切换后的状态：true=已收藏
  bool Toggle(const QString& path, const QString& root);
  void Remove(const QString& path);
  const QVector<Bookmark>& items() const;

  // 写回 settings 并落盘
  void Save() const;

 private:
  MdViewerSettings* settings_ = nullptr;
  BookmarkStore store_;
};

#endif  // FILE_APPS_QT_MD_VIEWER_BOOKMARKS_H_
