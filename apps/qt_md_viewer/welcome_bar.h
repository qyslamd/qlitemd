#ifndef FILE_APPS_QT_MD_VIEWER_WELCOME_BAR_H_
#define FILE_APPS_QT_MD_VIEWER_WELCOME_BAR_H_

#include <QString>
#include <QWidget>

class QLabel;
class QResizeEvent;
class QToolButton;

// 欢迎标签专用的工具栏内容：欢迎标识、当前工作区名、快速跳转与设置。
// 不放文档级控件，也不放搜索框（工作区搜索属于侧栏）。
class WelcomeBar : public QWidget {
  Q_OBJECT

 public:
  explicit WelcomeBar(QWidget* parent = nullptr);

  // 传工作区绝对路径；空串显示“未打开工作区”
  void SetWorkspaceName(const QString& root);
  void SetThemeDark(bool dark);

  QLabel* workspace_label() const { return workspace_label_; }
  QToolButton* quick_open_button() const { return quick_open_button_; }

 signals:
  void quickOpenRequested();

 protected:
  void resizeEvent(QResizeEvent* event) override;

 private:
  void UpdateWorkspaceText();

  QLabel* workspace_label_ = nullptr;
  QToolButton* quick_open_button_ = nullptr;
  QString workspace_display_;
  bool dark_ = false;
};

#endif  // FILE_APPS_QT_MD_VIEWER_WELCOME_BAR_H_
