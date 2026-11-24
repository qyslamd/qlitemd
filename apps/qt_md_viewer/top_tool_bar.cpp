#include "top_tool_bar.h"

#include <QApplication>
#include <QLayout>
#include <QPaintEvent>
#include <QPainter>
#include <QSizePolicy>

TopToolBar::TopToolBar(QWidget* parent) : QToolBar(parent) {
  setObjectName(QStringLiteral("topToolBar"));
  setAllowedAreas(Qt::TopToolBarArea);
  setContextMenuPolicy(Qt::PreventContextMenu);
  ResetInvariants();
  // 工具栏自己的布局默认带 style 给的边框内边距，这里清掉，
  // 让内容控件贴满 32px 的条
  setContentsMargins(0, 0, 0, 0);
  if (layout() != nullptr) {
    layout()->setContentsMargins(0, 0, 0, 0);
  }
}

void TopToolBar::ResetInvariants() {
  setMovable(false);
  setFloatable(false);
  setFixedHeight(32);
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  show();
}

void TopToolBar::paintEvent(QPaintEvent* event) {
  // 不调用基类：Fluent 会画工具栏自己的底色、边框和拖动手柄。
  // 用应用调色板的窗口色填一条，跟中心区保持一致
  QColor background = QApplication::palette().color(QPalette::Window);
  // DwmBlur 模式下窗口色带 alpha，直接填会透出 Mica，比中心区亮；
  // 这里强制不透明，和改造前文档条下方的窗口底色一致
  background.setAlpha(255);
  QPainter painter(this);
  painter.fillRect(event->rect(), background);
}
