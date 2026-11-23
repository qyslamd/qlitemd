#include "document_tab_bar.h"

#include <QAction>
#include <QContextMenuEvent>
#include <QLineF>
#include <QMenu>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QProxyStyle>
#include <QStyleOptionTab>
#include <QWindow>

namespace {

// 每个角（左下/右下）的形态，做法来自 qt-libui 的 ChromeTabAppearance
enum class CornerMode { kExtend, kNormal, kRetract };

CornerMode LeftCorner(const QStyleOptionTab* tab, bool selected) {
  CornerMode mode = CornerMode::kNormal;
  switch (tab->position) {
    case QStyleOptionTab::Middle:
    case QStyleOptionTab::End:
      mode = CornerMode::kExtend;
      break;
    default:
      break;
  }
  if (!selected &&
      tab->selectedPosition == QStyleOptionTab::PreviousIsSelected) {
    mode = CornerMode::kRetract;
  }
  return mode;
}

CornerMode RightCorner(const QStyleOptionTab* tab, bool selected) {
  CornerMode mode = CornerMode::kNormal;
  switch (tab->position) {
    case QStyleOptionTab::Beginning:
    case QStyleOptionTab::Middle:
      mode = CornerMode::kExtend;
      break;
    default:
      break;
  }
  if (!selected && tab->selectedPosition == QStyleOptionTab::NextIsSelected) {
    mode = CornerMode::kRetract;
  }
  return mode;
}

QPainterPath ChromeTabPath(const QStyleOptionTab* tab) {
  const QRectF rect = tab->rect;
  const qreal per = rect.height() / 5.0;  // Chrome 斜线水平偏移
  const bool selected = (tab->state & QStyle::State_Selected) != 0;

  qreal p1x = rect.left();
  qreal p2x = rect.left() + per;
  switch (LeftCorner(tab, selected)) {
    case CornerMode::kExtend:
      p1x = rect.left() - per;
      p2x = rect.left();
      break;
    case CornerMode::kNormal:
      p1x = rect.left();
      p2x = rect.left() + per;
      break;
    case CornerMode::kRetract:
      p1x = rect.left() + per;
      p2x = rect.left();
      break;
  }

  qreal p8x = rect.right();
  qreal p7x = rect.right() - per;
  switch (RightCorner(tab, selected)) {
    case CornerMode::kExtend:
      p8x = rect.right() + per;
      p7x = rect.right();
      break;
    case CornerMode::kNormal:
      p8x = rect.right();
      p7x = rect.right() - per;
      break;
    case CornerMode::kRetract:
      p8x = rect.right() - per;
      p7x = rect.right();
      break;
  }

  const QPointF p1(p1x, rect.bottom());
  const QPointF p2(p2x, rect.bottom() - per);
  const QPointF p3(p2x, rect.top() + per);
  const QPointF p4(p2x + per, rect.top());
  const QPointF p5(p7x - per, rect.top());
  const QPointF p6(p7x, rect.top() + per);
  const QPointF p7(p7x, rect.bottom() - per);
  const QPointF p8(p8x, rect.bottom());

  QPainterPath path;
  path.moveTo(p1);
  path.quadTo(QPointF(p2.x(), p1.y()), p2);
  path.lineTo(p3);
  path.quadTo(QPointF(p3.x(), p4.y()), p4);
  path.lineTo(p5);
  path.quadTo(QPointF(p6.x(), p5.y()), p6);
  path.lineTo(p7);
  path.quadTo(QPointF(p7.x(), p8.y()), p8);
  path.lineTo(p1);
  return path;
}

// Chrome 风标签：斜线形状 + 选中/悬停底色 + 未选中分隔线
class DocumentTabBarStyle : public QProxyStyle {
 public:
  void drawControl(ControlElement element, const QStyleOption* option,
                   QPainter* painter, const QWidget* widget) const override {
    const auto* tab = qstyleoption_cast<const QStyleOptionTab*>(option);
    if (tab == nullptr) {
      QProxyStyle::drawControl(element, option, painter, widget);
      return;
    }
    if (element == CE_TabBarTabLabel) {
      DrawLabel(tab, painter, widget);
      return;
    }
    if (element != CE_TabBarTabShape) {
      QProxyStyle::drawControl(element, option, painter, widget);
      return;
    }
    DrawShape(tab, painter);
  }

 private:
  void DrawLabel(const QStyleOptionTab* tab, QPainter* painter,
                 const QWidget* widget) const {
    painter->save();
    painter->setPen(tab->palette.color(
        (tab->state & QStyle::State_Selected) != 0 ? QPalette::HighlightedText
                                                   : QPalette::Text));
    QRect text_rect = subElementRect(SE_TabBarTabText, tab, widget);
    int left = tab->rect.x() + 10;
    // QStyleOptionTab 不一定带 icon；直接从所属标签条取
    QIcon icon = tab->icon;
    if (const auto* bar = qobject_cast<const QTabBar*>(widget)) {
      const int index = bar->tabAt(tab->rect.center());
      if (index >= 0) {
        icon = bar->tabIcon(index);
      }
    }
    const bool show_icon = !icon.isNull() && tab->rect.width() > 80;
    if (show_icon) {
      const QPixmap pixmap = icon.pixmap(QSize(16, 16));
      painter->drawPixmap(left, tab->rect.center().y() - pixmap.height() / 2,
                          pixmap);
      left += pixmap.width() + 6;
    }
    text_rect.setLeft(qMin(left, text_rect.right()));
    const QString text = painter->fontMetrics().elidedText(
        tab->text, Qt::ElideRight, text_rect.width());
    painter->drawText(text_rect,
                      Qt::AlignLeft | Qt::AlignVCenter | Qt::TextShowMnemonic,
                      text);
    painter->restore();
  }

  void DrawShape(const QStyleOptionTab* tab, QPainter* painter) const {
    const bool selected = (tab->state & QStyle::State_Selected) != 0;
    const bool hovered = (tab->state & QStyle::State_MouseOver) != 0;
    const QPalette palette = tab->palette;
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    const QPainterPath path = ChromeTabPath(tab);
    if (selected) {
      painter->fillPath(path, palette.color(QPalette::Base));
    } else if (hovered) {
      painter->fillPath(path, palette.color(QPalette::AlternateBase));
    }
    const bool hide_separator =
        selected || hovered ||
        tab->selectedPosition == QStyleOptionTab::NextIsSelected;
    const bool has_right_neighbor =
        tab->position == QStyleOptionTab::Beginning ||
        tab->position == QStyleOptionTab::Middle;
    if (!hide_separator && has_right_neighbor) {
      const QRectF rect = tab->rect;
      const qreal margin = rect.height() / 4.0;
      painter->setPen(QPen(palette.color(QPalette::Mid), 1));
      painter->drawLine(QLineF(rect.right(), rect.top() + margin, rect.right(),
                               rect.bottom() - margin));
    }
    painter->restore();
  }
};

}  // namespace

TabStripColors TabStripColorsFor(bool dark) {
  TabStripColors colors;
  if (dark) {
    colors.background = QColor(0x20, 0x21, 0x24);
    colors.selected = QColor(0x35, 0x36, 0x3a);
    colors.hover = QColor(255, 255, 255, 20);
    colors.separator = QColor(0x3c, 0x40, 0x43);
    colors.selected_text = QColor(0xe8, 0xea, 0xed);
    colors.normal_text = QColor(0x9a, 0xa0, 0xa6);
  } else {
    colors.background = QColor(0xde, 0xe1, 0xe6);
    colors.selected = QColor(0xff, 0xff, 0xff);
    colors.hover = QColor(0, 0, 0, 18);
    colors.separator = QColor(0xb8, 0xbc, 0xc2);
    colors.selected_text = QColor(0x20, 0x21, 0x24);
    colors.normal_text = QColor(0x4b, 0x4e, 0x54);
  }
  return colors;
}

DocumentTabBar::DocumentTabBar(QWidget* parent) : QTabBar(parent) {
  setObjectName(QStringLiteral("documentTabBar"));
  setDrawBase(false);
  // 标签少时等宽铺到偏好宽度；多了压到最小宽度再出滚动按钮
  setExpanding(true);
  setMovable(true);
  setTabsClosable(true);
  setElideMode(Qt::ElideRight);
  setUsesScrollButtons(true);
  setFocusPolicy(Qt::NoFocus);
  setStyle(new DocumentTabBarStyle);
  SetThemeDark(false);
}

void DocumentTabBar::SetThemeDark(bool dark) {
  const TabStripColors colors = TabStripColorsFor(dark);
  QPalette strip = palette();
  strip.setColor(QPalette::Window, colors.background);
  strip.setColor(QPalette::Base, colors.selected);
  strip.setColor(QPalette::AlternateBase, colors.hover);
  strip.setColor(QPalette::Mid, colors.separator);
  strip.setColor(QPalette::Text, colors.normal_text);
  strip.setColor(QPalette::HighlightedText, colors.selected_text);
  setPalette(strip);
  update();
}

void DocumentTabBar::paintEvent(QPaintEvent* event) {
  {
    QPainter painter(this);
    painter.fillRect(rect(), palette().color(QPalette::Window));
  }
  QTabBar::paintEvent(event);
}

void DocumentTabBar::RemoveCloseButton(int index) {
  setTabButton(index, QTabBar::RightSide, nullptr);
}

QSize DocumentTabBar::tabSizeHint(int) const {
  return QSize(kMaxTabWidth, kTabHeight);
}

QSize DocumentTabBar::minimumTabSizeHint(int) const {
  return QSize(kMinTabWidth, kMinTabHeight);
}

void DocumentTabBar::resizeEvent(QResizeEvent* event) {
  QTabBar::resizeEvent(event);
  ShowHideTabButtons();
}

void DocumentTabBar::tabInserted(int index) {
  QTabBar::tabInserted(index);
  ShowHideTabButtons();
}

void DocumentTabBar::tabRemoved(int index) {
  QTabBar::tabRemoved(index);
  ShowHideTabButtons();
}

void DocumentTabBar::ShowHideTabButtons() {
  for (int i = 0; i < count(); ++i) {
    if (QWidget* button = tabButton(i, QTabBar::RightSide)) {
      button->setVisible(tabRect(i).width() > kCloseButtonHideWidth);
    }
  }
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
