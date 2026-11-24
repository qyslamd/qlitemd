#ifndef FILE_APPS_QT_MD_VIEWER_TOP_TOOL_BAR_H_
#define FILE_APPS_QT_MD_VIEWER_TOP_TOOL_BAR_H_

#include <QToolBar>

// 窗口顶部的固定横带：横跨整窗、位于左右 dock 之上。
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
  // 基类会画工具栏背景/边框/手柄，这里全部扣掉，让窗口底色透出来
  void paintEvent(QPaintEvent* event) override;
};

#endif  // FILE_APPS_QT_MD_VIEWER_TOP_TOOL_BAR_H_
