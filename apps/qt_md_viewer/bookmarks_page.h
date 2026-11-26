#ifndef FILE_APPS_QT_MD_VIEWER_BOOKMARKS_PAGE_H_
#define FILE_APPS_QT_MD_VIEWER_BOOKMARKS_PAGE_H_

#include <QString>
#include <QVector>
#include <QWidget>

#include "bookmarks.h"

class QTreeWidget;
class BookmarkDelegate;

// 侧栏的“书签”页：双行卡片列表 + 右键移除。收藏集合由宿主提供。
class BookmarksPage : public QWidget {
  Q_OBJECT

 public:
  explicit BookmarksPage(QWidget* parent = nullptr);

  void SetBookmarks(const QVector<Bookmark>& bookmarks,
                    const QString& current_root);
  void SetThemeDark(bool dark);

 signals:
  void bookmarkActivated(const QString& absolute_path);
  void bookmarkRemoveRequested(const QString& absolute_path);

 private:
  void initUi();
  void initSignals();

  QTreeWidget* bookmark_tree_ = nullptr;
  BookmarkDelegate* tree_delegate_ = nullptr;
  bool theme_dark_ = false;
};

#endif  // FILE_APPS_QT_MD_VIEWER_BOOKMARKS_PAGE_H_
