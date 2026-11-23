#ifndef FILE_APPS_QT_MD_VIEWER_DOCUMENT_BAR_H_
#define FILE_APPS_QT_MD_VIEWER_DOCUMENT_BAR_H_

#include <QString>
#include <QWidget>

class QLabel;
class QLineEdit;
class QResizeEvent;
class QToolButton;

// 标题栏下的文档条：导航、路径、复制、搜索、阅读外观、缩放、设置。
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
  QLineEdit* search_edit() const { return search_edit_; }
  QToolButton* back_button() const { return back_button_; }
  QToolButton* forward_button() const { return forward_button_; }
  QToolButton* star_button() const { return star_button_; }
  QToolButton* copy_button() const { return copy_button_; }
  QToolButton* appearance_button() const { return appearance_button_; }
  QToolButton* zoom_out_button() const { return zoom_out_button_; }
  QToolButton* zoom_reset_button() const { return zoom_reset_button_; }
  QToolButton* zoom_in_button() const { return zoom_in_button_; }
  QToolButton* settings_button() const { return settings_button_; }

 protected:
  void resizeEvent(QResizeEvent* event) override;

 private:
  void UpdateChildVisibility();
  void UpdatePathElision();

  QString display_path_;
  bool dark_ = false;
  QWidget* zoom_group_ = nullptr;
  QLabel* path_label_ = nullptr;
  QLineEdit* search_edit_ = nullptr;
  QToolButton* back_button_ = nullptr;
  QToolButton* forward_button_ = nullptr;
  QToolButton* star_button_ = nullptr;
  QToolButton* copy_button_ = nullptr;
  QToolButton* appearance_button_ = nullptr;
  QToolButton* zoom_out_button_ = nullptr;
  QToolButton* zoom_reset_button_ = nullptr;
  QToolButton* zoom_in_button_ = nullptr;
  QToolButton* settings_button_ = nullptr;
};

#endif  // FILE_APPS_QT_MD_VIEWER_DOCUMENT_BAR_H_
