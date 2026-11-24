#include "document_tab_bar.h"

#include <QAction>
#include <QContextMenuEvent>
#include <QLineF>
#include <QLinearGradient>
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

// 形状照搬 YiGuai TabbarStyle::getSelectedShape：四角圆弧，选中标签按
// 位置向左右外扩，底部圆弧压进相邻标签，看起来更圆、更连贯
QPainterPath YiGuaiTabPath(const QStyleOptionTab* tab) {
  QRectF rect = tab->rect;
  rect.adjust(0, 2, 0, 0);
  const qreal len = rect.height() / 3.0;
  switch (tab->position) {
    case QStyleOptionTab::Beginning:
      rect.adjust(-len / 2.0, 0, len, 0);
      break;
    case QStyleOptionTab::Middle:
      rect.adjust(-len, 0, len, 0);
      break;
    case QStyleOptionTab::End:
      rect.adjust(-len, 0, len / 2.0, 0);
      break;
    case QStyleOptionTab::OnlyOneTab:
      rect.adjust(-len / 2.0, 0, len / 2.0, 0);
      break;
    default:
      break;
  }

  const qreal x = rect.x();
  const qreal y = rect.y();
  const qreal w = rect.width();
  const qreal h = rect.height();

  QPainterPath path;
  const QRectF bottom_left(x, y + h - len, len, len);
  path.moveTo(bottom_left.x() + bottom_left.width() / 2.0, y + h);
  path.arcTo(bottom_left, 270.0, 90.0);
  path.lineTo(path.currentPosition().x(), y + len / 2.0);

  const QRectF top_left(x + len, y, len, len);
  path.arcTo(top_left, 180.0, -90.0);
  path.lineTo(x + w - len - len / 2.0, y);

  const QRectF top_right(x + w - 2.0 * len, y, len, len);
  path.arcTo(top_right, 90.0, -90.0);
  path.lineTo(path.currentPosition().x(), y + h - len / 2.0);

  const QRectF bottom_right(path.currentPosition().x(), y + h - len, len, len);
  path.arcTo(bottom_right, 180.0, 90.0);
  return path;
}

// YiGuai 风标签：四角圆弧 + 选中/悬停底色 + 未选中分隔线
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
    const QColor base = tab->palette.color(
        (tab->state & QStyle::State_Selected) != 0 ? QPalette::HighlightedText
                                                   : QPalette::Text);
    painter->setPen(base);
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
    // 文字右端 80% 后渐隐，和 YiGuai 的标签文字一致
    QColor faded = base;
    faded.setAlpha(0);
    QLinearGradient gradient(QPointF(text_rect.left(), 0),
                             QPointF(text_rect.right(), 0));
    gradient.setColorAt(0.8, base);
    gradient.setColorAt(1.0, faded);
    painter->setPen(QPen(QBrush(gradient), 0));
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
    if (selected || hovered) {
      painter->fillPath(
          YiGuaiTabPath(tab),
          palette.color(selected ? QPalette::Base : QPalette::AlternateBase));
    } else {
      const QRectF rect = tab->rect;
      const qreal len = rect.height() / 3.0;
      if (tab->selectedPosition != QStyleOptionTab::NextIsSelected) {
        painter->setPen(QPen(palette.color(QPalette::Mid), 0.5));
        painter->drawLine(
            QLineF(rect.right() - len / 3.0, rect.top() + len / 2.0,
                   rect.right() - len / 3.0, rect.bottom() - len / 2.0));
      }
    }
    painter->restore();
  }
};

}  // namespace

TabStripColors TabStripColorsFor(bool dark) {
  TabStripColors colors;
  if (dark) {
    // YiGuai 暗色：框架 #2E2F30，选中 #3B3B3B
    colors.background = QColor(0x2e, 0x2f, 0x30);
    colors.selected = QColor(0x3b, 0x3b, 0x3b);
    colors.hover = QColor(0x59, 0x59, 0x59, 76);
    colors.separator = QColor(0xd3, 0xd3, 0xd5);
    colors.selected_text = QColor(Qt::white);
    colors.normal_text = QColor(0xd3, 0xd3, 0xd5);
  } else {
    // YiGuai 浅色：框架 #CECECE，选中 #F9F9FB
    colors.background = QColor(0xce, 0xce, 0xce);
    colors.selected = QColor(0xf9, 0xf9, 0xfb);
    colors.hover = QColor(255, 255, 255, 76);
    colors.separator = QColor(0x4b, 0x4b, 0x4b);
    colors.selected_text = QColor(Qt::black);
    colors.normal_text = QColor(0x66, 0x66, 0x66);
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
