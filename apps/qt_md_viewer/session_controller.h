#ifndef FILE_APPS_QT_MD_VIEWER_SESSION_CONTROLLER_H_
#define FILE_APPS_QT_MD_VIEWER_SESSION_CONTROLLER_H_

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVector>

#include "side_view.h"

struct MdViewerSettings;

// 标签顺序与激活下标的编解码。第 0 项固定是欢迎标签（路径为空）；
// 其余项是已打开的文档路径，路径唯一。宿主写会话时整表重建，
// 因此这里只保留 Add/SetActive/序列化，没有关闭与重排语义。
class TabSessionCodec {
 public:
  TabSessionCodec();

  // 空路径返回欢迎标签；已存在返回现有下标；否则追加并返回新下标。
  int Add(const QString& path);
  bool SetActive(int index);

  int active() const { return active_; }
  int count() const { return paths_.size(); }
  QString path_at(int index) const;
  bool IsWelcome(int index) const { return index == 0; }

  QByteArray Serialize() const;
  bool Restore(const QByteArray& data);

 private:
  QStringList paths_;  // 第 0 项恒为空
  int active_ = 0;
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
