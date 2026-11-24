#ifndef FILE_APPS_QT_MD_VIEWER_ACTIVITY_BAR_H_
#define FILE_APPS_QT_MD_VIEWER_ACTIVITY_BAR_H_

#include <QVector>
#include <QWidget>

class QButtonGroup;
class QToolButton;

// 左侧活动栏：固定 30px，只放图标，负责切换侧栏的视图。
// 点击当前已选中的项时仍会发 viewActivated，宿主据此收起侧栏。
class ActivityBar : public QWidget {
  Q_OBJECT

 public:
  enum View {
    kDocuments = 0,
    kSearch = 1,
    kBookmarks = 2,
    kRenderStatus = 3,
  };

  explicit ActivityBar(QWidget* parent = nullptr);

  void SetThemeDark(bool dark);
  void SetActiveView(int view);
  int active_view() const { return active_view_; }

  QToolButton* button(int view) const;

 signals:
  void viewActivated(int view);

 protected:
  // 当前视图用左侧强调条标出来
  void paintEvent(QPaintEvent* event) override;

 private:
  QButtonGroup* group_ = nullptr;
  QVector<QToolButton*> buttons_;
  int active_view_ = kDocuments;
};

#endif  // FILE_APPS_QT_MD_VIEWER_ACTIVITY_BAR_H_
