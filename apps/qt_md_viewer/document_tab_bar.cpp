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
#include <QTimer>
#include <QWindow>

namespace {

// 四角圆弧路径；选中与悬停的矩形不同，路径本身共用
QPainterPath RoundedTabPath(const QRectF& rect) {
  const qreal x = rect.x();
  const qreal y = rect.y();
  const qreal w = rect.width();
  const qreal h = rect.height();
  const qreal len = h / 3.0;

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

// 选中标签：顶部内缩 2px，按位置向左右外扩
QPainterPath SelectedTabPath(const QStyleOptionTab* tab) {
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
  return RoundedTabPath(rect);
}

// 悬停标签：不做顶部内缩，外扩量与选中不同
QPainterPath HoveredTabPath(const QStyleOptionTab* tab) {
  QRectF rect = tab->rect;
  const qreal len = rect.height() / 3.0;
  switch (tab->position) {
    case QStyleOptionTab::Beginning:
      rect.adjust(-len / 2.0, 0, len, 0);
      break;
    case QStyleOptionTab::Middle:
      rect.adjust(-len, 0, len, 0);
      break;
    case QStyleOptionTab::End:
      rect.adjust(-len / 2.0, 0, len / 2.0, 0);
      if (tab->selectedPosition == QStyleOptionTab::PreviousIsSelected) {
        rect.adjust(-len / 2.0, 0, 0, 0);
      }
      break;
    default:
      break;
  }
  return RoundedTabPath(rect);
}

// 标签样式：四角圆弧 + 选中/悬停底色 + 未选中分隔线
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
    const auto* bar = qobject_cast<const QTabBar*>(widget);
    const int index = bar != nullptr ? bar->tabAt(tab->rect.center()) : -1;
    QIcon icon = tab->icon;
    if (bar != nullptr && index >= 0) {
      icon = bar->tabIcon(index);
    }
    const bool loading =
        bar != nullptr && index >= 0 && bar->tabData(index).toBool();
    const bool show_icon = !icon.isNull() && tab->rect.width() > 80;
    if (show_icon) {
      const QPixmap pixmap = icon.pixmap(QSize(16, 16));
      const QRectF icon_rect(left, tab->rect.center().y() - pixmap.height() / 2,
                             pixmap.width(), pixmap.height());
      if (loading) {
        // 渲染中：图标位置画 270° 旋转弧
        int angle = 160;
        if (const auto* document_bar =
                qobject_cast<const DocumentTabBar*>(bar)) {
          angle = document_bar->spin_angle();
        }
        const bool dark =
            tab->palette.color(QPalette::Window).lightness() < 128;
        QLinearGradient spin(icon_rect.topLeft(), icon_rect.bottomRight());
        if (dark) {
          spin.setColorAt(0.0, Qt::white);
          spin.setColorAt(0.7, QColor(0x40, 0xe0, 0xd0));
          spin.setColorAt(1.0, QColor(0xad, 0xd8, 0xe6));
        } else {
          spin.setColorAt(0.0, QColor(0x00, 0x00, 0x8b));
          spin.setColorAt(0.7, Qt::gray);
          spin.setColorAt(1.0, Qt::white);
        }
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);
        painter->setPen(
            QPen(QBrush(spin), 2.0, Qt::DotLine, Qt::RoundCap, Qt::RoundJoin));
        painter->setBrush(Qt::NoBrush);
        painter->drawArc(icon_rect.adjusted(-2, -2, 2, 2), angle, 4320);
        painter->restore();
      } else {
        painter->drawPixmap(icon_rect.topLeft(), pixmap);
      }
      left += pixmap.width() + 6;
    }
    // 窄标签只留图标/弧线，文字已经放不下
    if (tab->rect.width() <= 60) {
      painter->restore();
      return;
    }
    text_rect.setLeft(qMin(left, text_rect.right()));
    const QString text = painter->fontMetrics().elidedText(
        tab->text, Qt::ElideRight, text_rect.width());
    // 文字右端 80% 后渐隐，长标题不会被硬边截断
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
    if (selected) {
      painter->fillPath(SelectedTabPath(tab), palette.color(QPalette::Base));
    } else if (hovered) {
      painter->fillPath(HoveredTabPath(tab),
                        palette.color(QPalette::AlternateBase));
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
    // 暗色：框架 #2E2F30，选中 #3B3B3B
    colors.background = QColor(0x2e, 0x2f, 0x30);
    colors.selected = QColor(0x3b, 0x3b, 0x3b);
    colors.hover = QColor(0x59, 0x59, 0x59, 76);
    colors.separator = QColor(0xd3, 0xd3, 0xd5);
    colors.selected_text = QColor(Qt::white);
    colors.normal_text = QColor(0xd3, 0xd3, 0xd5);
  } else {
    // 浅色：框架 #CECECE，选中 #F9F9FB
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
  // 加载态弧线：每 40ms 转 10°
  spin_timer_ = new QTimer(this);
  spin_timer_->setInterval(40);
  connect(spin_timer_, &QTimer::timeout, this, [this] {
    spin_angle_ = (spin_angle_ + 160) % (360 * 16);
    update();
  });
  SetThemeDark(false);
}

void DocumentTabBar::SetTabLoading(int index, bool loading) {
  if (index < 0 || index >= count()) {
    return;
  }
  setTabData(index, loading);
  UpdateSpinTimer();
  update();
}

void DocumentTabBar::UpdateSpinTimer() {
  bool any_loading = false;
  for (int i = 0; i < count(); ++i) {
    if (tabData(i).toBool()) {
      any_loading = true;
      break;
    }
  }
  if (any_loading) {
    if (!spin_timer_->isActive()) {
      spin_timer_->start();
    }
  } else if (spin_timer_->isActive()) {
    spin_timer_->stop();
  }
}

bool DocumentTabBar::IsTabLoading(int index) const {
  if (index < 0 || index >= count()) {
    return false;
  }
  return tabData(index).toBool();
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
  if (QWidget* button = tabButton(index, QTabBar::RightSide)) {
    button->setMinimumSize(18, 18);
  }
  ShowHideTabButtons();
}

void DocumentTabBar::tabRemoved(int index) {
  QTabBar::tabRemoved(index);
  // 关掉正在渲染的标签也要停掉旋转定时器
  UpdateSpinTimer();
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
  // 标签条空白区没有可操作项，不弹空菜单
  if (index < 0) {
    return;
  }
  QMenu menu(this);
  QAction* close_action = menu.addAction(tr("Close Tab"));
  QAction* close_others_action = menu.addAction(tr("Close Other Tabs"));
  QAction* close_right_action = menu.addAction(tr("Close Tabs to the Right"));
  // 欢迎标签不可关闭；没有其它/右侧标签时对应项禁用
  if (index == 0) {
    close_action->setEnabled(false);
  }
  close_others_action->setEnabled(count() > 2);
  close_right_action->setEnabled(index < count() - 1);
  menu.addSeparator();
  QAction* reload_action = menu.addAction(tr("Reload"));
  QAction* reopen_action = menu.addAction(tr("Reopen Closed Tab"));
  QAction* chosen = menu.exec(event->globalPos());
  if (chosen == nullptr) {
    return;
  }
  if (chosen == close_action) {
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
  } else if (chosen == reload_action) {
    emit reloadTabRequested(index);
  } else if (chosen == reopen_action) {
    emit reopenClosedTabRequested();
  }
}
