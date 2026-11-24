#ifndef FILE_APPS_QT_MD_VIEWER_BOOKMARK_CONTROLLER_H_
#define FILE_APPS_QT_MD_VIEWER_BOOKMARK_CONTROLLER_H_

#include <QString>
#include <QVector>

#include "bookmarks.h"

struct MdViewerSettings;

// 书签集合与持久化。星标按钮、侧栏列表等 UI 同步仍由宿主负责。
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

#endif  // FILE_APPS_QT_MD_VIEWER_BOOKMARK_CONTROLLER_H_
