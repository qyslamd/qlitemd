#ifndef FILE_APPS_QT_MD_VIEWER_DOCUMENT_TAB_BAR_H_
#define FILE_APPS_QT_MD_VIEWER_DOCUMENT_TAB_BAR_H_

#include <QColor>
#include <QTabBar>

class QContextMenuEvent;
class QMouseEvent;
class QPaintEvent;
class QResizeEvent;
class QTimer;

// Chrome 标签条配色；选中标签用页面色，标签条本身用框架色
struct TabStripColors {
  QColor background;
  QColor selected;
  QColor hover;
  QColor separator;
  QColor selected_text;
  QColor normal_text;
};

TabStripColors TabStripColorsFor(bool dark);

// 选中标签的底色。工具栏复用同一个值，标签和工具栏才能连成一片。
inline QColor SelectedTabColorFor(bool dark) {
  return dark ? QColor(0x3b, 0x3b, 0x3b) : QColor(0xf9, 0xf9, 0xfb);
}

// 标题栏里的文档标签条：等宽、超出宽度后滚动、自绘 Chrome 外观。
// 欢迎标签由宿主放在第 0 位并用 RemoveCloseButton 去掉关闭按钮。
class DocumentTabBar : public QTabBar {
  Q_OBJECT

 public:
  explicit DocumentTabBar(QWidget* parent = nullptr);

  void RemoveCloseButton(int index);
  void SetThemeDark(bool dark);
  // 标签正在渲染：图标位置画旋转弧
  void SetTabLoading(int index, bool loading);
  bool IsTabLoading(int index) const;
  int spin_angle() const { return spin_angle_; }

  QSize tabSizeHint(int index) const override;
  QSize minimumTabSizeHint(int index) const override;

 signals:
  // 中键关闭；左键关闭走 QTabBar::tabCloseRequested
  void closeTabRequested(int index);
  void reloadTabRequested(int index);
  void reopenClosedTabRequested();

 protected:
  void paintEvent(QPaintEvent* event) override;
  void resizeEvent(QResizeEvent* event) override;
  void tabInserted(int index) override;
  void tabRemoved(int index) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;
  void contextMenuEvent(QContextMenuEvent* event) override;

 private:
  void ShowHideTabButtons();
  void UpdateSpinTimer();

  // 偏好 220x38；大量标签时等宽压到 36 再出滚动按钮，高度不低于 30
  static constexpr int kTabHeight = 38;
  static constexpr int kMinTabWidth = 36;
  static constexpr int kMinTabHeight = 30;
  static constexpr int kMaxTabWidth = 220;
  // 标签宽度低于该值时藏掉关闭按钮，避免文字与按钮互挤
  static constexpr int kCloseButtonHideWidth = 40;
  QTimer* spin_timer_ = nullptr;
  int spin_angle_ = 0;
};

#endif  // FILE_APPS_QT_MD_VIEWER_DOCUMENT_TAB_BAR_H_
