#ifndef FILE_APPS_QT_MD_VIEWER_DOCUMENT_TAB_BAR_H_
#define FILE_APPS_QT_MD_VIEWER_DOCUMENT_TAB_BAR_H_

#include <QTabBar>

class QContextMenuEvent;
class QMouseEvent;

// 标题栏里的文档标签条：等宽、超出宽度后滚动、自绘 Chrome 外观。
// 欢迎标签由宿主放在第 0 位并用 RemoveCloseButton 去掉关闭按钮。
class DocumentTabBar : public QTabBar {
  Q_OBJECT

 public:
  explicit DocumentTabBar(QWidget* parent = nullptr);

  void RemoveCloseButton(int index);

  QSize tabSizeHint(int index) const override;
  QSize minimumTabSizeHint(int index) const override;

 signals:
  // 中键关闭；左键关闭走 QTabBar::tabCloseRequested
  void closeTabRequested(int index);
  void newTabRequested();

 protected:
  void mousePressEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;
  void contextMenuEvent(QContextMenuEvent* event) override;

 private:
  static constexpr int kTabHeight = 34;
  static constexpr int kMinTabWidth = 120;
  static constexpr int kMaxTabWidth = 220;
};

#endif  // FILE_APPS_QT_MD_VIEWER_DOCUMENT_TAB_BAR_H_
