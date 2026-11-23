#include "document_tab_bar.h"

#include <QAction>
#include <QContextMenuEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QProxyStyle>
#include <QStyleOptionTab>
#include <QWindow>

namespace {

// Chrome 风标签：选中态用页面底色，未选中用窗口底色，顶部圆角
class DocumentTabBarStyle : public QProxyStyle {
 public:
  void drawControl(ControlElement element, const QStyleOption* option,
                   QPainter* painter, const QWidget* widget) const override {
    if (element != CE_TabBarTabShape) {
      QProxyStyle::drawControl(element, option, painter, widget);
      return;
    }
    const auto* tab = qstyleoption_cast<const QStyleOptionTab*>(option);
    if (tab == nullptr) {
      return;
    }
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    const QRectF rect = QRectF(tab->rect).adjusted(1, 4, -1, 0);
    QPainterPath path;
    path.addRoundedRect(rect, 6, 6);
    const QPalette palette = tab->palette;
    QColor fill = palette.color(QPalette::Window);
    if ((tab->state & State_Selected) != 0) {
      fill = palette.color(QPalette::Base);
    } else if ((tab->state & State_MouseOver) != 0) {
      fill = fill.lighter(105);
    }
    painter->fillPath(path, fill);
    painter->restore();
  }
};

}  // namespace

DocumentTabBar::DocumentTabBar(QWidget* parent) : QTabBar(parent) {
  setObjectName(QStringLiteral("documentTabBar"));
  setDrawBase(false);
  setExpanding(true);
  setMovable(true);
  setTabsClosable(true);
  setElideMode(Qt::ElideRight);
  setUsesScrollButtons(true);
  setFocusPolicy(Qt::NoFocus);
  setStyle(new DocumentTabBarStyle);
}

void DocumentTabBar::RemoveCloseButton(int index) {
  setTabButton(index, QTabBar::RightSide, nullptr);
}

QSize DocumentTabBar::tabSizeHint(int index) const {
  QSize size = QTabBar::tabSizeHint(index);
  size.setHeight(kTabHeight);
  size.setWidth(qBound(kMinTabWidth, size.width(), kMaxTabWidth));
  return size;
}

QSize DocumentTabBar::minimumTabSizeHint(int) const {
  return QSize(kMinTabWidth, kTabHeight);
}

void DocumentTabBar::mousePressEvent(QMouseEvent* event) {
  // 标签条吃掉标题栏的空白区；按在空白处拖动窗口（Qt 5.15+）
  if (event->button() == Qt::LeftButton && tabAt(event->pos()) < 0) {
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
    QWidget* top = window();
    if (top != nullptr && top->windowHandle() != nullptr) {
      top->windowHandle()->startSystemMove();
      return;
    }
#endif
  }
  QTabBar::mousePressEvent(event);
}

void DocumentTabBar::mouseReleaseEvent(QMouseEvent* event) {
  if (event->button() == Qt::MiddleButton) {
    const int index = tabAt(event->pos());
    if (index >= 0) {
      emit closeTabRequested(index);
      return;
    }
  }
  QTabBar::mouseReleaseEvent(event);
}

void DocumentTabBar::contextMenuEvent(QContextMenuEvent* event) {
  const int index = tabAt(event->pos());
  QMenu menu(this);
  QAction* new_action = menu.addAction(QStringLiteral("新建标签页"));
  QAction* close_action = nullptr;
  QAction* close_others_action = nullptr;
  QAction* close_right_action = nullptr;
  if (index >= 0) {
    menu.addSeparator();
    close_action = menu.addAction(QStringLiteral("关闭标签页"));
    close_others_action = menu.addAction(QStringLiteral("关闭其他标签页"));
    close_right_action = menu.addAction(QStringLiteral("关闭右侧标签页"));
    // 欢迎标签不可关闭
    if (index == 0) {
      close_action->setEnabled(false);
    }
  }
  QAction* chosen = menu.exec(event->globalPos());
  if (chosen == nullptr) {
    return;
  }
  if (chosen == new_action) {
    emit newTabRequested();
  } else if (chosen == close_action) {
    emit closeTabRequested(index);
  } else if (chosen == close_others_action) {
    for (int i = count() - 1; i >= 0; --i) {
      if (i != index && i != 0) {
        emit closeTabRequested(i);
      }
    }
  } else if (chosen == close_right_action) {
    for (int i = count() - 1; i > index; --i) {
      emit closeTabRequested(i);
    }
  }
}
