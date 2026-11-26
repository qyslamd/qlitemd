#ifndef FILE_APPS_QT_MD_VIEWER_TOP_TOOL_BAR_H_
#define FILE_APPS_QT_MD_VIEWER_TOP_TOOL_BAR_H_

#include <QString>
#include <QToolBar>
#include <QWidget>

class QLabel;
class QLineEdit;
class QMenu;
class QPaintEvent;
class QResizeEvent;
class QToolButton;

// 窗口顶部的固定横带：横跨整窗、位于侧栏与大纲之上。
// 只当容器用，不提供拖动、浮动、关闭；绘制由本类接管，避免 Fluent 给
// QToolBar 画背景、边框和拖动手柄。
class TopToolBar : public QToolBar {
  Q_OBJECT

 public:
  explicit TopToolBar(QWidget* parent = nullptr);

  // restoreState 会把工具条的可见性与几何一起改写，恢复后按这个压回
  // 不变量
  void ResetInvariants();

 protected:
  void paintEvent(QPaintEvent* event) override;
};

// 横带在文档态的页：导航、路径、复制、查找、大纲、阅读外观、缩放、导出。
// 是窗口级控件（作用于当前标签），不属于任何 ReaderPage。
class DocumentBar : public QWidget {
  Q_OBJECT

 public:
  explicit DocumentBar(QWidget* parent = nullptr);

  // display_path 是根内相对/根外绝对的展示串；tooltip 给完整路径
  void SetPath(const QString& display_path, const QString& tooltip);
  // 换主题时重载动作类图标；星标由窗口按收藏状态单独刷新
  void SetThemeDark(bool dark);

  QLabel* path_label() const { return path_label_; }
  QLineEdit* find_edit() const { return find_edit_; }
  QToolButton* back_button() const { return back_button_; }
  QToolButton* forward_button() const { return forward_button_; }
  QToolButton* star_button() const { return star_button_; }
  QToolButton* copy_button() const { return copy_button_; }
  // 导出是下拉按钮：菜单项由窗口在 createActions 之后挂上去（共用
  // File 菜单那两条 QAction），本类只负责按钮与图标
  QToolButton* export_button() const { return export_button_; }
  QMenu* export_menu() const { return export_menu_; }
  QToolButton* outline_button() const { return outline_button_; }
  QToolButton* appearance_button() const { return appearance_button_; }
  QToolButton* zoom_out_button() const { return zoom_out_button_; }
  QToolButton* zoom_reset_button() const { return zoom_reset_button_; }
  QToolButton* zoom_in_button() const { return zoom_in_button_; }

 protected:
  void resizeEvent(QResizeEvent* event) override;

 private:
  void UpdateChildVisibility();
  void UpdatePathElision();

  QString display_path_;
  bool dark_ = false;
  QWidget* zoom_group_ = nullptr;
  QWidget* export_separator_ = nullptr;
  QLabel* path_label_ = nullptr;
  QLineEdit* find_edit_ = nullptr;
  QToolButton* back_button_ = nullptr;
  QToolButton* forward_button_ = nullptr;
  QToolButton* star_button_ = nullptr;
  QToolButton* copy_button_ = nullptr;
  QToolButton* export_button_ = nullptr;
  QMenu* export_menu_ = nullptr;
  QToolButton* outline_button_ = nullptr;
  QToolButton* appearance_button_ = nullptr;
  QToolButton* zoom_out_button_ = nullptr;
  QToolButton* zoom_reset_button_ = nullptr;
  QToolButton* zoom_in_button_ = nullptr;
};

// 横带在欢迎态的页：欢迎标识、当前工作区名与快速跳转。
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

#endif  // FILE_APPS_QT_MD_VIEWER_TOP_TOOL_BAR_H_
