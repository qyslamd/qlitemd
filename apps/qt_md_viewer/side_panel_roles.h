#ifndef FILE_APPS_QT_MD_VIEWER_SIDE_PANEL_ROLES_H_
#define FILE_APPS_QT_MD_VIEWER_SIDE_PANEL_ROLES_H_

#include <Qt>

// 侧栏各页面放在 QTreeWidgetItem 上的自定义角色。集中一处，页面与
// delegate 共用；测试也按这些角色断言。
constexpr int kPathRole = Qt::UserRole;
constexpr int kRelativePathRole = Qt::UserRole + 1;
constexpr int kIsDirectoryRole = Qt::UserRole + 2;
constexpr int kMissingRole = Qt::UserRole + 3;
constexpr int kSearchHitRole = Qt::UserRole + 4;
constexpr int kSizeRole = Qt::UserRole + 5;
constexpr int kCountRole = Qt::UserRole + 6;
constexpr int kBookmarkedRole = Qt::UserRole + 7;
constexpr int kBookmarkDisplayPathRole = Qt::UserRole + 8;
constexpr int kQueryLengthRole = Qt::UserRole + 9;

#endif  // FILE_APPS_QT_MD_VIEWER_SIDE_PANEL_ROLES_H_
