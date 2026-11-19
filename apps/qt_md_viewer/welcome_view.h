#ifndef FILE_EXAMPLES_MD_VIEWER_WELCOME_VIEW_H_
#define FILE_EXAMPLES_MD_VIEWER_WELCOME_VIEW_H_

#include <QStringList>
#include <QWidget>

class QLabel;
class QListWidget;

// 没有已提交文档时的落地页：打开入口、最近文档、快捷键提示。
// 只发信号，不碰文件系统策略；打开动作由宿主执行。
class WelcomeView : public QWidget {
  Q_OBJECT

 public:
  explicit WelcomeView(QWidget* parent = nullptr);

  // 过滤不存在的路径，去重并只保留最近 5 条
  void SetRecentFiles(const QStringList& paths, const QString& root_dir);

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
  QLabel* recent_title_ = nullptr;
  QLabel* empty_hint_ = nullptr;
  QListWidget* recent_list_ = nullptr;
};

#endif  // FILE_EXAMPLES_MD_VIEWER_WELCOME_VIEW_H_
