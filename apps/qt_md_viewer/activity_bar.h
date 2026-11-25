#ifndef FILE_APPS_QT_MD_VIEWER_ACTIVITY_BAR_H_
#define FILE_APPS_QT_MD_VIEWER_ACTIVITY_BAR_H_

#include <QVector>
#include <QWidget>

#include "side_view.h"

class QButtonGroup;
class QToolButton;

// 左侧活动栏：固定 42px（与标题栏高度一致），只放图标，负责切换侧栏的视图。
// 点击当前已选中的项时仍会发 viewActivated，宿主据此收起侧栏。
class ActivityBar : public QWidget {
  Q_OBJECT

 public:
  explicit ActivityBar(QWidget* parent = nullptr);

  void SetThemeDark(bool dark);
  void SetActiveView(SideView view);
  SideView active_view() const { return active_view_; }

  QToolButton* button(SideView view) const;
  QToolButton* settings_button() const { return settings_button_; }

 signals:
  void viewActivated(SideView view);
  void settingsRequested();

 protected:
  // 当前视图用左侧强调条标出来
  void paintEvent(QPaintEvent* event) override;

 private:
  QButtonGroup* group_ = nullptr;
  QVector<QToolButton*> buttons_;
  QToolButton* settings_button_ = nullptr;
  SideView active_view_ = SideView::kDocuments;
};

#endif  // FILE_APPS_QT_MD_VIEWER_ACTIVITY_BAR_H_
