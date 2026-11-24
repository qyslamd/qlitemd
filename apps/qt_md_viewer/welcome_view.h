#ifndef FILE_APPS_QT_MD_VIEWER_WELCOME_VIEW_H_
#define FILE_APPS_QT_MD_VIEWER_WELCOME_VIEW_H_

#include <QStringList>
#include <QWidget>

class QLabel;
class QListWidget;
class RecentItemDelegate;

// 没有已提交文档时的落地页：打开入口、最近文档、快捷键提示。
// 只发信号，不碰文件系统策略；打开动作由宿主执行。
class WelcomeView : public QWidget {
  Q_OBJECT

 public:
  explicit WelcomeView(QWidget* parent = nullptr);

  // 过滤不存在的路径，去重并只保留最近 5 条
  void SetRecentFiles(const QStringList& paths, const QString& root_dir);
  // 两套主题图标（svg/light、svg/dark）：切主题后由窗口调用，最近列表
  // 重绘时按 flag 取对应一套
  void SetThemeDark(bool dark);

 signals:
  void openFileRequested();
  void openDirectoryRequested();
  void recentActivated(const QString& path);
  void quickOpenRequested();

 protected:
  void paintEvent(QPaintEvent* event) override;

 private:
  void RebuildRecentItems();

  QStringList recent_paths_;
  QString root_dir_;
  bool theme_dark_ = false;
  RecentItemDelegate* recent_delegate_ = nullptr;
  QLabel* recent_title_ = nullptr;
  QLabel* empty_hint_ = nullptr;
  QListWidget* recent_list_ = nullptr;
};

#endif  // FILE_APPS_QT_MD_VIEWER_WELCOME_VIEW_H_
