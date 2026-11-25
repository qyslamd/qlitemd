#ifndef FILE_APPS_QT_MD_VIEWER_SIDE_VIEW_H_
#define FILE_APPS_QT_MD_VIEWER_SIDE_VIEW_H_

#include <QMetaType>
#include <QString>

// 侧栏视图的稳定 id：活动栏按钮、侧栏页面和持久化共用一份枚举。
// 数值写进 qt_md_viewer.ini 的 window/sidebar_view，只能追加，不能
// 插入或重排。
enum class SideView {
  kDocuments = 0,
  kSearch = 1,
  kBookmarks = 2,
  kRenderStatus = 3,
};

constexpr int kSideViewCount = 4;

Q_DECLARE_METATYPE(SideView)

bool IsValidSideView(int value);
// 越界值退回 fallback，用于读取旧配置或外部传入的下标
SideView SideViewFromInt(int value, SideView fallback = SideView::kDocuments);
QString SideViewTitle(SideView view);

// 侧栏/大纲的宽度与可见性；窗口持有活动副本，退出时写入会话
struct ShellLayoutState {
  SideView view = SideView::kDocuments;
  bool sidebar_visible = true;
  int sidebar_width = 280;
  int outline_width = 212;
  bool outline_visible = true;
};

#endif  // FILE_APPS_QT_MD_VIEWER_SIDE_VIEW_H_
