#ifndef FILE_APPS_QT_MD_VIEWER_SESSION_CONTROLLER_H_
#define FILE_APPS_QT_MD_VIEWER_SESSION_CONTROLLER_H_

#include <QByteArray>
#include <QString>
#include <QVector>

#include "side_view.h"

struct MdViewerSettings;

// 侧栏/大纲的宽度与可见性；窗口关闭时整体写入会话
struct ShellLayoutState {
  SideView view = SideView::kDocuments;
  bool sidebar_visible = true;
  int sidebar_width = 280;
  int outline_width = 212;
  bool outline_visible = true;
};

// 会话持久化：窗口几何、侧栏布局、标签顺序与激活项、根目录和上次文件。
// 偏好设置不在这里；宿主仍直接读 MdViewerSettings。
class SessionController {
 public:
  SessionController(MdViewerSettings* settings, bool session_owner);

  bool IsOwner() const { return owner_; }
  // 恢复期间不把半成品写回
  bool IsRestoring() const { return restoring_; }
  void BeginRestore() { restoring_ = true; }
  void EndRestore() { restoring_ = false; }

  // 启动读取
  bool RestoreRequested() const;
  QByteArray SavedGeometry() const;
  ShellLayoutState SavedShellLayout() const;
  // 保存的标签路径（第 0 项为空）；没有可恢复的多标签会话时返回空，
  // 并把 active 置 0
  QVector<QString> SavedTabPaths(int* active = nullptr) const;
  QString SavedRoot() const;
  QString SavedLastFile() const;

  // 运行期写入。几何与布局只改内存，退出时随 SaveAll 落盘。
  void SaveGeometry(const QByteArray& geometry);
  void SaveShellLayout(const ShellLayoutState& layout);
  // 标签顺序 + 激活项 + 上次文件，立即落盘
  void SaveTabs(const QVector<QString>& paths, int active);
  // 以下都只改会话字段，随后由 SaveTabs/ClearWorkspace 落盘
  void SetSidebarVisible(bool visible);
  // 切到某个视图时侧栏必然展开
  void SetSidebarView(SideView view);
  void SetOutlineVisible(bool visible);
  void SetRoot(const QString& root);
  void MarkWorkspaceOpened(const QString& path);
  void ClearWorkspace();
  void SaveAll() const;

 private:
  void SaveSessionIfAllowed() const;

  MdViewerSettings* settings_ = nullptr;
  bool owner_ = false;
  bool restoring_ = false;
};

#endif  // FILE_APPS_QT_MD_VIEWER_SESSION_CONTROLLER_H_
