#include "exteachingtip.h"
#include "fluentui3colors.h"
#include <fluentui3styleproperties.h>

#include <QApplication>
#include <QCloseEvent>
#include <QEasingCurve>
#include <QEvent>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPainterPathStroker>
#include <QPixmap>
#include <QPushButton>
#include <QRegion>
#include <QScrollArea>
#include <QScrollBar>
#include <QSizePolicy>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QVariantAnimation>
#include <QtMath>

namespace {
constexpr int Tail = 10;
constexpr int Shadow = 16;
constexpr int SurfaceInset = Shadow + Tail;
constexpr int Margin = 8;
constexpr int Gap = 4;
constexpr int PreferredWidth = 320 + 2 * SurfaceInset;

// 对齐方式只影响切向偏移，定位与尾巴共用同一主方向。
ExTeachingTip::Placement placementSide(ExTeachingTip::Placement placement) {
  using P = ExTeachingTip::Placement;
  switch (placement) {
  case P::Auto:
  case P::TopLeft:
  case P::TopRight:
    return P::Top;
  case P::BottomLeft:
  case P::BottomRight:
    return P::Bottom;
  case P::LeftTop:
  case P::LeftBottom:
    return P::Left;
  case P::RightTop:
  case P::RightBottom:
    return P::Right;
  default:
    return placement;
  }
}

class TeachingTipCloseButton final : public QToolButton {
public:
  using QToolButton::QToolButton;

protected:
  void paintEvent(QPaintEvent *event) override {
    QToolButton::paintEvent(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    // 不缓存平台的标题栏图标，实时使用 Qt 调色板，跟随明暗主题与禁用状态。
    painter.setPen(QPen(
        palette().color(isEnabled() ? QPalette::Active : QPalette::Disabled,
                        QPalette::ButtonText),
        1.35, Qt::SolidLine, Qt::RoundCap));
    const QPointF center = QRectF(rect()).center();
    painter.drawLine(center + QPointF(-4, -4), center + QPointF(4, 4));
    painter.drawLine(center + QPointF(4, -4), center + QPointF(-4, 4));
  }
};
} // namespace

// 这里只初始化数据，不创建 QObject 子控件，避免 d_ptr 赋值前重入事件处理。
class ExTeachingTipPrivate {
public:
  using Placement = ExTeachingTip::Placement;
  using TailVisibility = ExTeachingTip::TailVisibility;
  using HeroContentPlacement = ExTeachingTip::HeroContentPlacement;
  using CloseReason = ExTeachingTip::CloseReason;

  QPointer<QWidget> m_target;
  QPointer<QWidget> m_previousFocus;
  QMetaObject::Connection m_targetDestroyedConnection;
  QLabel *m_titleLabel = nullptr;
  QLabel *m_subtitleLabel = nullptr;
  QPushButton *m_actionButton = nullptr;
  QPushButton *m_footerCloseButton = nullptr;
  QToolButton *m_closeButton = nullptr;
  QScrollArea *m_scrollArea = nullptr;
  QWidget *m_customContainer = nullptr;
  QWidget *m_heroContainer = nullptr;
  QWidget *m_footer = nullptr;
  QLabel *m_iconLabel = nullptr;
  QVBoxLayout *m_contentLayout = nullptr;
  QPointer<QWidget> m_customContent;
  QPointer<QWidget> m_heroContent;
  QIcon m_iconSource;
  TailVisibility m_tailVisibility = TailVisibility::Auto;
  HeroContentPlacement m_heroPlacement = HeroContentPlacement::Auto;
  QMargins m_placementMargin;
  CloseReason m_closeReason = CloseReason::Programmatic;
  bool m_closing = false;
  bool m_animationEnabled = true;
  QVariantAnimation *m_openAnimation = nullptr;
  QPixmap m_openSnapshot;
  qreal m_openScale = 1.0;
  bool m_openAnimationActive = false;
  QPixmap m_shadow;
  QPainterPath m_shadowSurface;
  bool m_shadowDark = false;
  Placement m_preferredPlacement = Placement::Auto;
  Placement m_actualPlacement = Placement::Top;
  QPoint m_anchor;
  bool m_lightDismissEnabled = false;
  bool m_open = false;
  bool m_placementScheduled = false;
};

ExTeachingTip::ExTeachingTip(QWidget *parent)
    : QWidget(parent), d_ptr(new ExTeachingTipPrivate) {
  Q_D(ExTeachingTip);
  setFocusPolicy(Qt::StrongFocus);
  setAttribute(Qt::WA_StyledBackground, false);
  auto *root = new QVBoxLayout(this);
  root->setContentsMargins(SurfaceInset + 1, SurfaceInset + 1, SurfaceInset + 1,
                           SurfaceInset + 1);
  root->setSizeConstraint(QLayout::SetNoConstraint);

  // 内容过高时内部滚动，不挤压宿主布局，也不越过宿主窗口边界。
  d->m_scrollArea = new QScrollArea(this);
  d->m_scrollArea->setFrameShape(QFrame::NoFrame);
  d->m_scrollArea->setWidgetResizable(true);
  QSizePolicy scrollPolicy = d->m_scrollArea->sizePolicy();
  scrollPolicy.setRetainSizeWhenHidden(true);
  d->m_scrollArea->setSizePolicy(scrollPolicy);
  d->m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  d->m_scrollArea->setAutoFillBackground(false);
  d->m_scrollArea->viewport()->setAutoFillBackground(false);
  root->addWidget(d->m_scrollArea);
  auto *content = new QWidget;
  auto *layout = new QVBoxLayout(content);
  d->m_contentLayout = layout;
  layout->setSpacing(10);
  d->m_heroContainer = new QWidget(content);
  auto *heroLayout = new QVBoxLayout(d->m_heroContainer);
  heroLayout->setContentsMargins(0, 0, 0, 0);
  layout->addWidget(d->m_heroContainer);
  d->m_heroContainer->hide();
  auto *header = new QHBoxLayout;
  d->m_iconLabel = new QLabel(content);
  d->m_iconLabel->setFixedSize(20, 20);
  header->addWidget(d->m_iconLabel, 0, Qt::AlignTop);
  d->m_iconLabel->hide();
  d->m_titleLabel = new QLabel(content);
  d->m_titleLabel->setTextFormat(Qt::PlainText);
  d->m_titleLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
  d->m_titleLabel->setWordWrap(true);
  QFont titleFont = font();
  titleFont.setBold(true);
  d->m_titleLabel->setFont(titleFont);
  header->addWidget(d->m_titleLabel, 1);
  d->m_closeButton = new TeachingTipCloseButton(content);
  d->m_closeButton->setAutoRaise(true);
  d->m_closeButton->setAccessibleName(tr("关闭提示"));
  d->m_closeButton->setToolTip(tr("关闭提示"));
  d->m_closeButton->setFixedSize(32, 32);
  header->addWidget(d->m_closeButton, 0, Qt::AlignTop);
  layout->addLayout(header);
  d->m_subtitleLabel = new QLabel(content);
  d->m_subtitleLabel->setTextFormat(Qt::PlainText);
  d->m_subtitleLabel->setSizePolicy(QSizePolicy::Ignored,
                                    QSizePolicy::Preferred);
  d->m_subtitleLabel->setWordWrap(true);
  d->m_subtitleLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
  layout->addWidget(d->m_subtitleLabel);
  d->m_customContainer = new QWidget(content);
  auto *customLayout = new QVBoxLayout(d->m_customContainer);
  customLayout->setContentsMargins(0, 0, 0, 0);
  layout->addWidget(d->m_customContainer);
  d->m_customContainer->hide();
  d->m_footer = new QWidget(content);
  auto *footerLayout = new QHBoxLayout(d->m_footer);
  footerLayout->setContentsMargins(0, 0, 0, 0);
  footerLayout->setSpacing(8);
  d->m_actionButton = new QPushButton(d->m_footer);
  setActionButtonAccent(true);
  d->m_footerCloseButton = new QPushButton(d->m_footer);
  footerLayout->addWidget(d->m_actionButton);
  footerLayout->addWidget(d->m_footerCloseButton);
  layout->addWidget(d->m_footer);
  d->m_subtitleLabel->hide();
  updateButtons();
  d->m_scrollArea->setWidget(content);
  content->setAutoFillBackground(false);
  const auto closeClicked = [this]() {
    QPointer<ExTeachingTip> guard(this);
    Q_EMIT closeButtonClick();
    if (guard)
      requestClose(CloseReason::CloseButton);
  };
  connect(d->m_closeButton, &QToolButton::clicked, this, closeClicked);
  connect(d->m_footerCloseButton, &QPushButton::clicked, this, closeClicked);
  connect(d->m_actionButton, &QPushButton::clicked, this,
          &ExTeachingTip::actionButtonClick);

  d->m_openAnimation = new QVariantAnimation(this);
  d->m_openAnimation->setStartValue(0.01);
  d->m_openAnimation->setEndValue(1.0);
  d->m_openAnimation->setDuration(167);
  d->m_openAnimation->setEasingCurve(QEasingCurve::OutCubic);
  connect(d->m_openAnimation, &QVariantAnimation::valueChanged, this,
          [this](const QVariant &value) {
            Q_D(ExTeachingTip);
            d->m_openScale = value.toReal();
            update();
          });
  connect(d->m_openAnimation, &QVariantAnimation::finished, this,
          [this]() { stopOpenAnimation(); });
  // 作为页面子控件时不能随父页面一起显示；只通过 showAt()/setIsOpen(true) 打开。
  QWidget::setVisible(false);
}

ExTeachingTip::~ExTeachingTip() {
  Q_D(ExTeachingTip);
  disconnect(d->m_targetDestroyedConnection);
  if (qApp)
    qApp->removeEventFilter(this);
}

QString ExTeachingTip::title() const { return d_func()->m_titleLabel->text(); }
void ExTeachingTip::setTitle(QString title) {
  Q_D(ExTeachingTip);
  if (d->m_titleLabel->text() == title)
    return;
  d->m_titleLabel->setText(title);
  setAccessibleName(title);
  schedulePlacement();
}
QString ExTeachingTip::subtitle() const {
  return d_func()->m_subtitleLabel->text();
}
void ExTeachingTip::setSubtitle(QString subtitle) {
  Q_D(ExTeachingTip);
  if (d->m_subtitleLabel->text() == subtitle)
    return;
  d->m_subtitleLabel->setText(subtitle);
  d->m_subtitleLabel->setVisible(!subtitle.isEmpty());
  setAccessibleDescription(subtitle);
  schedulePlacement();
}
QString ExTeachingTip::actionButtonContent() const {
  return d_func()->m_actionButton->text();
}
void ExTeachingTip::setActionButtonContent(QString text) {
  Q_D(ExTeachingTip);
  if (d->m_actionButton->text() == text)
    return;
  d->m_actionButton->setText(text);
  updateButtons();
  schedulePlacement();
}
bool ExTeachingTip::actionButtonAccent() const {
  Q_D(const ExTeachingTip);
  return d->m_actionButton->property(ButtonAccentStyleProperty).toBool();
}

void ExTeachingTip::setActionButtonAccent(bool accent) {
  Q_D(ExTeachingTip);
  if (actionButtonAccent() == accent)
    return;
  d->m_actionButton->setProperty(ButtonAccentStyleProperty, accent);
  d->m_actionButton->update();
}

QString ExTeachingTip::closeButtonContent() const {
  return d_func()->m_footerCloseButton->text();
}
void ExTeachingTip::setCloseButtonContent(QString text) {
  Q_D(ExTeachingTip);
  if (d->m_footerCloseButton->text() == text)
    return;
  d->m_footerCloseButton->setText(text);
  updateButtons();
  schedulePlacement();
}
QPushButton *ExTeachingTip::actionButton() const {
  return d_func()->m_actionButton;
}
QPushButton *ExTeachingTip::closeButton() const {
  return d_func()->m_footerCloseButton;
}

void ExTeachingTip::updateButtons() {
  Q_D(ExTeachingTip);
  const bool footerClose =
      !d->m_footerCloseButton->text().isEmpty() && !d->m_lightDismissEnabled;
  const bool action = !d->m_actionButton->text().isEmpty();
  d->m_closeButton->setVisible(!d->m_lightDismissEnabled && !footerClose);
  d->m_footerCloseButton->setVisible(footerClose);
  d->m_actionButton->setVisible(action);
  d->m_footer->setVisible(action || footerClose);
}

QWidget *ExTeachingTip::content() const {
  return d_func()->m_customContent.data();
}
void ExTeachingTip::setContent(QWidget *content) {
  Q_D(ExTeachingTip);
  replaceContent(d->m_customContent, content, d->m_customContainer);
}
QWidget *ExTeachingTip::heroContent() const {
  return d_func()->m_heroContent.data();
}
void ExTeachingTip::setHeroContent(QWidget *content) {
  Q_D(ExTeachingTip);
  replaceContent(d->m_heroContent, content, d->m_heroContainer);
}
void ExTeachingTip::replaceContent(QPointer<QWidget> &current,
                                   QWidget *replacement, QWidget *container) {
  Q_D(ExTeachingTip);
  // 禁止把内部控件、另一个内容槽或祖先重新接入，避免父子循环。
  if (replacement == current ||
      (replacement &&
       (replacement == this || replacement->isAncestorOf(this) ||
        isAncestorOf(replacement) || replacement == d->m_target ||
        (d->m_target && replacement->isAncestorOf(d->m_target)))))
    return;
  if (current) {
    current->hide();
    container->layout()->removeWidget(current);
    current->deleteLater();
  }
  current = replacement;
  if (replacement) {
    replacement->setParent(container);
    container->layout()->addWidget(replacement);
    replacement->show();
  }
  container->setVisible(replacement != nullptr);
  schedulePlacement();
}
ExTeachingTip::HeroContentPlacement
ExTeachingTip::heroContentPlacement() const {
  return d_func()->m_heroPlacement;
}
void ExTeachingTip::setHeroContentPlacement(HeroContentPlacement placement) {
  Q_D(ExTeachingTip);
  if (placement < HeroContentPlacement::Auto ||
      placement > HeroContentPlacement::Bottom ||
      d->m_heroPlacement == placement)
    return;
  d->m_heroPlacement = placement;
  d->m_contentLayout->removeWidget(d->m_heroContainer);
  d->m_contentLayout->insertWidget(placement == HeroContentPlacement::Bottom
                                       ? d->m_contentLayout->count() - 1
                                       : 0,
                                   d->m_heroContainer);
  schedulePlacement();
}
QIcon ExTeachingTip::iconSource() const { return d_func()->m_iconSource; }
void ExTeachingTip::setIconSource(QIcon icon) {
  Q_D(ExTeachingTip);
  if (d->m_iconSource.cacheKey() == icon.cacheKey())
    return;
  d->m_iconSource = icon;
  d->m_iconLabel->setPixmap(
      icon.pixmap(20, 20, isEnabled() ? QIcon::Normal : QIcon::Disabled));
  d->m_iconLabel->setVisible(!icon.isNull());
  schedulePlacement();
}
ExTeachingTip::TailVisibility ExTeachingTip::tailVisibility() const {
  return d_func()->m_tailVisibility;
}
void ExTeachingTip::setTailVisibility(TailVisibility visibility) {
  Q_D(ExTeachingTip);
  if (visibility < TailVisibility::Auto ||
      visibility > TailVisibility::Collapsed ||
      d->m_tailVisibility == visibility)
    return;
  d->m_tailVisibility = visibility;
  update();
}
QMargins ExTeachingTip::placementMargin() const {
  return d_func()->m_placementMargin;
}
void ExTeachingTip::setPlacementMargin(QMargins margin) {
  Q_D(ExTeachingTip);
  if (d->m_placementMargin == margin)
    return;
  d->m_placementMargin = margin;
  schedulePlacement();
}
QWidget *ExTeachingTip::target() const { return d_func()->m_target.data(); }
void ExTeachingTip::setTarget(QWidget *target) {
  Q_D(ExTeachingTip);
  if (target == d->m_target || target == this ||
      (target && isAncestorOf(target)))
    return;
  disconnect(d->m_targetDestroyedConnection);
  d->m_target = target;
  if (d->m_target)
    d->m_targetDestroyedConnection =
        connect(d->m_target.data(), &QObject::destroyed, this,
                &ExTeachingTip::forceClose);
  if (d->m_open)
    setVisible(true);
}
ExTeachingTip::Placement ExTeachingTip::preferredPlacement() const {
  return d_func()->m_preferredPlacement;
}
void ExTeachingTip::setPreferredPlacement(Placement placement) {
  Q_D(ExTeachingTip);
  if (placement < Auto || placement > Center ||
      d->m_preferredPlacement == placement)
    return;
  d->m_preferredPlacement = placement;
  schedulePlacement();
}
bool ExTeachingTip::isLightDismissEnabled() const {
  return d_func()->m_lightDismissEnabled;
}
void ExTeachingTip::setIsLightDismissEnabled(bool enabled) {
  Q_D(ExTeachingTip);
  if (d->m_lightDismissEnabled == enabled)
    return;
  d->m_lightDismissEnabled = enabled;
  updateButtons();
  schedulePlacement();
}
bool ExTeachingTip::isOpen() const { return d_func()->m_open; }

bool ExTeachingTip::isAnimationEnabled() const {
  return d_func()->m_animationEnabled;
}

void ExTeachingTip::setAnimationEnabled(bool enabled) {
  Q_D(ExTeachingTip);
  if (d->m_animationEnabled == enabled)
    return;
  d->m_animationEnabled = enabled;
  if (!enabled && d->m_openAnimationActive)
    stopOpenAnimation();
}

void ExTeachingTip::showAt(QWidget *target) {
  if (target == this || (target && isAncestorOf(target)))
    return;
  QPointer<ExTeachingTip> guard(this);
  setTarget(target);
  if (guard && !isOpen())
    setIsOpen(true);
}

void ExTeachingTip::setIsOpen(bool open) { setVisible(open); }

void ExTeachingTip::setVisible(bool visible) {
  Q_D(ExTeachingTip);
  if (!visible) {
    requestClose(CloseReason::Programmatic);
    return;
  }
  if (d->m_target && !d->m_target->isVisible()) {
    forceClose();
    return;
  }
  QWidget *host = d->m_target      ? d->m_target->window()
                  : parentWidget() ? parentWidget()->window()
                                   : nullptr;
  if (!host || !host->isVisible())
    return;
  if (parentWidget() != host) {
    QPointer<ExTeachingTip> guard(this);
    setParent(host, Qt::Widget);
    if (!guard)
      return;
  }
  if (!updatePlacement()) {
    QWidget::setVisible(false);
    return;
  }
  if (!d->m_open)
    d->m_previousFocus = QApplication::focusWidget();
  raise();
  QWidget::setVisible(true);
}

void ExTeachingTip::dismiss() { requestClose(CloseReason::Programmatic); }

void ExTeachingTip::requestClose(CloseReason reason) {
  Q_D(ExTeachingTip);
  if (d->m_closing)
    return;
  if (!d->m_open) {
    QWidget::setVisible(false);
    return;
  }
  d->m_closing = true;
  bool cancel = false;
  QPointer<ExTeachingTip> guard(this);
  Q_EMIT closing(reason, &cancel);
  if (!guard)
    return;
  d->m_closing = false;
  if (cancel)
    return;
  d->m_closeReason = reason;
  QWidget::setVisible(false);
}

void ExTeachingTip::forceClose() {
  Q_D(ExTeachingTip);
  d->m_closeReason = CloseReason::Programmatic;
  QWidget::setVisible(false);
}

void ExTeachingTip::startOpenAnimation() {
  Q_D(ExTeachingTip);
  if (!d->m_animationEnabled)
    return;
  stopOpenAnimation();
  if (width() <= 0 || height() <= 0)
    return;

  if (layout())
    layout()->activate();
  // 首次显示时，仅激活外层布局还不足以更新内容尺寸和滚动条容器。
  // 在截图前同步完成布局请求，避免把排队等待隐藏的临时滚动条画进快照。
  d->m_contentLayout->activate();
  QEvent scrollLayoutRequest(QEvent::LayoutRequest);
  QCoreApplication::sendEvent(d->m_scrollArea, &scrollLayoutRequest);
  d->m_contentLayout->activate();
  const qreal dpr = devicePixelRatioF();
  d->m_openSnapshot = QPixmap(qCeil(width() * dpr), qCeil(height() * dpr));
  d->m_openSnapshot.setDevicePixelRatio(dpr);
  d->m_openSnapshot.fill(Qt::transparent);
  {
    QPainter snapshotPainter(&d->m_openSnapshot);
    // 不绘制 QWidget 的窗口背景，避免把宿主页面复制进透明阴影区域。
    QWidget::render(&snapshotPainter, QPoint(), QRegion(),
                    QWidget::DrawChildren);
  }
  if (d->m_openSnapshot.isNull())
    return;

  d->m_openScale = 0.01;
  d->m_openAnimationActive = true;
  // 实际内容暂时隐藏，但 QSizePolicy 保留布局尺寸；动画不改变真实控件树。
  d->m_scrollArea->hide();
  d->m_openAnimation->start();
  update();
}

void ExTeachingTip::stopOpenAnimation() {
  Q_D(ExTeachingTip);
  // 构造期间也可能同步收到 hideEvent，此时动画对象尚未创建。
  if (!d->m_openAnimation)
    return;
  if (d->m_openAnimation->state() != QAbstractAnimation::Stopped)
    d->m_openAnimation->stop();
  d->m_openAnimationActive = false;
  d->m_openScale = 1.0;
  d->m_openSnapshot = QPixmap();
  d->m_scrollArea->show();
  update();
}

void ExTeachingTip::closeEvent(QCloseEvent *event) {
  Q_D(ExTeachingTip);
  QPointer<ExTeachingTip> guard(this);
  requestClose(CloseReason::Programmatic);
  if (guard && d->m_open)
    event->ignore();
  else
    event->accept();
}

QRect ExTeachingTip::visibleTargetRect() const {
  Q_D(const ExTeachingTip);
  QWidget *host = parentWidget();
  if (!host || !d->m_target || d->m_target->window() != host ||
      !d->m_target->isVisible())
    return {};
  QRect rect(d->m_target->mapTo(host, QPoint()), d->m_target->size());
  // 包含 QScrollArea 的 viewport 裁剪；目标滚出页面时不留下一只悬空气泡。
  for (QWidget *ancestor = d->m_target->parentWidget(); ancestor;
       ancestor = ancestor->parentWidget()) {
    rect = rect.intersected(
        QRect(ancestor->mapTo(host, QPoint()), ancestor->size()));
    if (ancestor == host)
      break;
  }
  return rect;
}

bool ExTeachingTip::updatePlacement() {
  Q_D(ExTeachingTip);
  const QRect targetRect = visibleTargetRect();
  if (d->m_target && targetRect.isEmpty())
    return false;
  if (!parentWidget())
    return false;
  const QRect bounds =
      parentWidget()->rect().adjusted(Margin, Margin, -Margin, -Margin);
  if (bounds.width() < 120 || bounds.height() < 120)
    return false;
  d->m_customContainer->setVisible(!d->m_customContent.isNull());
  d->m_heroContainer->setVisible(!d->m_heroContent.isNull());
  const int w = qMin(PreferredWidth, bounds.width());
  const int contentWidth =
      qMax(1, w - 2 * (SurfaceInset + 1) -
                  style()->pixelMetric(QStyle::PM_ScrollBarExtent));
  const int contentHeight =
      d->m_contentLayout->totalHeightForWidth(contentWidth);
  const int desiredHeight =
      qMax(70, contentHeight >= 0 ? contentHeight
                                  : d->m_contentLayout->sizeHint().height()) +
      2 * (SurfaceInset + 1);
  const int h = qMin(bounds.height(), desiredHeight);
  const int dx = d->m_placementMargin.left() - d->m_placementMargin.right();
  const int dy = d->m_placementMargin.top() - d->m_placementMargin.bottom();

  if (!d->m_target) {
    // WinUI 支持无 Target 的提示；Qt 版本需要显式的父窗口作为根。
    int x = bounds.right() - w + 1 - d->m_placementMargin.right();
    int y = bounds.bottom() - h + 1 - d->m_placementMargin.bottom();
    d->m_actualPlacement = d->m_preferredPlacement;
    switch (d->m_preferredPlacement) {
    case Top:
      x = bounds.center().x() - w / 2 + dx;
      y = bounds.top() + d->m_placementMargin.top();
      break;
    case Bottom:
      x = bounds.center().x() - w / 2 + dx;
      break;
    case Left:
      x = bounds.left() + d->m_placementMargin.left();
      y = bounds.center().y() - h / 2 + dy;
      break;
    case Right:
      y = bounds.center().y() - h / 2 + dy;
      break;
    case TopLeft:
    case LeftTop:
      x = bounds.left() + d->m_placementMargin.left();
      y = bounds.top() + d->m_placementMargin.top();
      break;
    case TopRight:
    case RightTop:
      y = bounds.top() + d->m_placementMargin.top();
      break;
    case BottomLeft:
    case LeftBottom:
      x = bounds.left() + d->m_placementMargin.left();
      break;
    case Center:
      x = bounds.center().x() - w / 2 + dx;
      y = bounds.center().y() - h / 2 + dy;
      break;
    default:
      break;
    }
    setGeometry(qBound(bounds.left(), x, bounds.right() - w + 1),
                qBound(bounds.top(), y, bounds.bottom() - h + 1), w, h);
    switch (placementSide(d->m_actualPlacement)) {
    case Bottom:
      d->m_anchor = QPoint(w / 2, 0);
      break;
    case Left:
      d->m_anchor = QPoint(w, h / 2);
      break;
    case Right:
      d->m_anchor = QPoint(0, h / 2);
      break;
    default:
      d->m_anchor = QPoint(w / 2, h);
      break;
    }
    update();
    return true;
  }

  const auto candidate = [&](Placement placement) {
    int x = targetRect.center().x() - w / 2 + dx;
    int y = targetRect.center().y() - h / 2 + dy;
    switch (placementSide(placement)) {
    case Bottom:
      y = targetRect.bottom() + 1 + Gap - Shadow +
          d->m_placementMargin.bottom();
      break;
    case Left:
      x = targetRect.left() - w - Gap + Shadow - d->m_placementMargin.left();
      break;
    case Right:
      x = targetRect.right() + 1 + Gap - Shadow + d->m_placementMargin.right();
      break;
    case Center:
      break;
    default:
      y = targetRect.top() - h - Gap + Shadow - d->m_placementMargin.top();
      break;
    }
    switch (placement) {
    case TopRight:
    case BottomRight:
      x = targetRect.center().x() - w / 4 + dx;
      break;
    case TopLeft:
    case BottomLeft:
      x = targetRect.center().x() - 3 * w / 4 + dx;
      break;
    case LeftTop:
    case RightTop:
      y = targetRect.center().y() - 3 * h / 4 + dy;
      break;
    case LeftBottom:
    case RightBottom:
      y = targetRect.center().y() - h / 4 + dy;
      break;
    default:
      break;
    }
    return QRect(x, y, w, h);
  };
  // 切向坐标会被限制到窗口内，斜向候选与对应主方向的可见面积相同，不必重复尝试。
  QList<Placement> choices{Top, Bottom, Left, Right};
  if (d->m_preferredPlacement != Auto) {
    choices.removeAll(d->m_preferredPlacement);
    choices.prepend(d->m_preferredPlacement);
  }
  QRect best;
  int bestArea = -1;
  for (Placement placement : choices) {
    QRect rect = candidate(placement);
    const Placement side = placementSide(placement);
    const bool horizontal = side == Left || side == Right;
    if (horizontal)
      rect.moveTop(qBound(bounds.top(), rect.top(), bounds.bottom() - h + 1));
    else
      rect.moveLeft(qBound(bounds.left(), rect.left(), bounds.right() - w + 1));
    const QRect intersection = rect.intersected(bounds);
    const int area = intersection.width() * intersection.height();
    if (area > bestArea) {
      best = rect;
      bestArea = area;
      d->m_actualPlacement = placement;
    }
    if (bounds.contains(rect))
      break;
  }
  best.moveLeft(qBound(bounds.left(), best.left(), bounds.right() - w + 1));
  best.moveTop(qBound(bounds.top(), best.top(), bounds.bottom() - h + 1));
  setGeometry(best);
  d->m_anchor = targetRect.center() - best.topLeft();
  update();
  return true;
}

void ExTeachingTip::schedulePlacement() {
  Q_D(ExTeachingTip);
  if (!d->m_open || d->m_placementScheduled)
    return;
  d->m_placementScheduled = true;
  QTimer::singleShot(0, this, [this, d]() {
    d->m_placementScheduled = false;
    if (!d->m_open)
      return;
    const QSize previousSize = size();
    const Placement previousPlacement = d->m_actualPlacement;
    if (!updatePlacement()) {
      forceClose();
      return;
    }
    // 显示后宿主通常会补发一次 LayoutRequest；位置不变时不能因此终止动画。
    // 只有快照尺寸或揭示方向已经失效，才切回实时内容。
    if (d->m_openAnimationActive &&
        (size() != previousSize || d->m_actualPlacement != previousPlacement))
      stopOpenAnimation();
  });
}

bool ExTeachingTip::event(QEvent *event) {
  Q_D(ExTeachingTip);
  if (event->type() == QEvent::FontChange && d->m_titleLabel) {
    QFont titleFont = font();
    titleFont.setBold(true);
    d->m_titleLabel->setFont(titleFont);
  }
  // 显示期间的 LayoutRequest（含子控件）统一由应用级过滤器处理。
  if (event->type() == QEvent::FontChange ||
      event->type() == QEvent::StyleChange)
    schedulePlacement();
  if ((event->type() == QEvent::StyleChange ||
       event->type() == QEvent::PaletteChange ||
       event->type() == QEvent::EnabledChange) &&
      d->m_closeButton) {
    d->m_closeButton->update();
    if (d->m_iconLabel)
      d->m_iconLabel->setPixmap(d->m_iconSource.pixmap(
          20, 20, isEnabled() ? QIcon::Normal : QIcon::Disabled));
    update();
  }
  return QWidget::event(event);
}

bool ExTeachingTip::eventFilter(QObject *watched, QEvent *event) {
  Q_D(ExTeachingTip);
  if (!d->m_open)
    return false;
  // 应用级过滤器只处理定位和关闭相关事件，跳过绘制、鼠标移动等高频事件。
  switch (event->type()) {
  case QEvent::ShortcutOverride:
  case QEvent::KeyPress:
  case QEvent::MouseButtonPress:
  case QEvent::Wheel:
  case QEvent::Hide:
  case QEvent::Close:
  case QEvent::ParentChange:
  case QEvent::Move:
  case QEvent::Resize:
  case QEvent::LayoutRequest:
    break;
  default:
    return false;
  }
  auto *widget = qobject_cast<QWidget *>(watched);
  if (!widget)
    return false;
  const bool ownWidget = widget == this || isAncestorOf(widget);
  const bool sameWindow = widget->window() == window();
  if (ownWidget && event->type() == QEvent::LayoutRequest &&
      !d->m_openAnimationActive)
    schedulePlacement();
  if (sameWindow &&
      (event->type() == QEvent::ShortcutOverride ||
       event->type() == QEvent::KeyPress) &&
      static_cast<QKeyEvent *>(event)->key() == Qt::Key_Escape) {
    if (event->type() == QEvent::ShortcutOverride)
      event->accept();
    else
      requestClose(CloseReason::LightDismiss);
    return true;
  }
  if ((event->type() == QEvent::MouseButtonPress ||
       event->type() == QEvent::Wheel) &&
      sameWindow && !ownWidget && d->m_lightDismissEnabled) {
    QPointer<QWidget> receiver(widget);
    requestClose(CloseReason::LightDismiss);
    return receiver.isNull();
  }
  const bool targetAncestor =
      widget == parentWidget() ||
      (d->m_target &&
       (widget == d->m_target || widget->isAncestorOf(d->m_target)));
  if (targetAncestor) {
    if (event->type() == QEvent::Hide || event->type() == QEvent::Close ||
        event->type() == QEvent::ParentChange) {
      QPointer<QWidget> receiver(widget);
      forceClose();
      return receiver.isNull();
    }
    if (event->type() == QEvent::Move || event->type() == QEvent::Resize ||
        event->type() == QEvent::LayoutRequest)
      schedulePlacement();
  }
  return false;
}

void ExTeachingTip::showEvent(QShowEvent *event) {
  Q_D(ExTeachingTip);
  QWidget::showEvent(event);
  if (!d->m_open) {
    // 同一个提示再次打开时从顶部开始，避免沿用上次滚动位置而看不到标题和叉号。
    d->m_scrollArea->verticalScrollBar()->setValue(0);
    d->m_open = true;
    d->m_closeReason = CloseReason::Programmatic;
    qApp->installEventFilter(this);
    QPointer<ExTeachingTip> guard(this);
    Q_EMIT isOpenChanged(true);
    if (guard && d->m_open)
      Q_EMIT opened();
    if (guard && d->m_open)
      startOpenAnimation();
  }
}

void ExTeachingTip::hideEvent(QHideEvent *event) {
  Q_D(ExTeachingTip);
  stopOpenAnimation();
  QWidget::hideEvent(event);
  if (!d->m_open)
    return;
  d->m_open = false;
  qApp->removeEventFilter(this);
  QPointer<ExTeachingTip> guard(this);
  const QPointer<QWidget> previousFocus = d->m_previousFocus;
  d->m_previousFocus.clear();
  QWidget *focus = QApplication::focusWidget();
  if ((!focus || focus == this || isAncestorOf(focus)) && previousFocus &&
      previousFocus->isVisible() && previousFocus->isEnabled() &&
      previousFocus->window() == window() && window()->isActiveWindow())
    previousFocus->setFocus(Qt::OtherFocusReason);
  if (!guard)
    return;
  const CloseReason reason = d->m_closeReason;
  Q_EMIT isOpenChanged(false);
  if (guard && !d->m_open)
    Q_EMIT closed(reason);
}

void ExTeachingTip::paintEvent(QPaintEvent *) {
  Q_D(ExTeachingTip);
  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing);
  if (d->m_openAnimationActive) {
    const QRectF fullRect(0.0, 0.0, width(), height());
    const QRectF body = fullRect.adjusted(SurfaceInset, SurfaceInset,
                                          -SurfaceInset, -SurfaceInset);
    const qreal scale = qBound(0.01, d->m_openScale, 1.0);
    QPointF origin = body.center();
    if (d->m_target) {
      const qreal insetX = qMin(body.width() / 2.0, 13.0);
      const qreal insetY = qMin(body.height() / 2.0, 13.0);
      const qreal anchorX = qBound(body.left() + insetX, qreal(d->m_anchor.x()),
                                   body.right() - insetX);
      const qreal anchorY = qBound(body.top() + insetY, qreal(d->m_anchor.y()),
                                   body.bottom() - insetY);
      switch (placementSide(d->m_actualPlacement)) {
      case Bottom:
        origin = QPointF(anchorX, body.top());
        break;
      case Left:
        origin = QPointF(body.right(), anchorY);
        break;
      case Right:
        origin = QPointF(body.left(), anchorY);
        break;
      default:
        origin = QPointF(anchorX, body.bottom());
        break;
      }
    }
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.translate(origin);
    painter.scale(scale, scale);
    painter.translate(-origin);
    painter.drawPixmap(0, 0, d->m_openSnapshot);
    return;
  }
  const QRectF body = QRectF(rect()).adjusted(SurfaceInset, SurfaceInset,
                                              -SurfaceInset, -SurfaceInset);
  if (body.width() < 32 || body.height() < 32)
    return;
  const qreal cornerRadius =
      5.0; // 面板圆角：在这里统一调整，边框和阴影共用此轮廓。
  QPainterPath surface;
  surface.addRoundedRect(body, cornerRadius, cornerRadius);
  QPainterPath tail;
  const qreal insetX = qMin(body.width() / 2, cornerRadius + 8);
  const qreal insetY = qMin(body.height() / 2, cornerRadius + 8);
  const qreal x = qBound(body.left() + insetX, qreal(d->m_anchor.x()),
                         body.right() - insetX);
  const qreal y = qBound(body.top() + insetY, qreal(d->m_anchor.y()),
                         body.bottom() - insetY);
  const Placement side = !d->m_target && d->m_actualPlacement == Center
                             ? Top
                             : placementSide(d->m_actualPlacement);
  const bool hasTail =
      d->m_tailVisibility != TailVisibility::Collapsed &&
      (d->m_target || d->m_tailVisibility == TailVisibility::Visible);
  // 无目标默认不显示尾巴；空间不足时不画方向错误的尾巴。
  if (hasTail && side == Top && d->m_anchor.y() > body.bottom()) {
    tail.moveTo(x - 8, body.bottom() - 1);
    tail.lineTo(x, body.bottom() + Tail - 1);
    tail.lineTo(x + 8, body.bottom() - 1);
  } else if (hasTail && side == Bottom && d->m_anchor.y() < body.top()) {
    tail.moveTo(x - 8, body.top() + 1);
    tail.lineTo(x, body.top() - Tail + 1);
    tail.lineTo(x + 8, body.top() + 1);
  } else if (hasTail && side == Left && d->m_anchor.x() > body.right()) {
    tail.moveTo(body.right() - 1, y - 8);
    tail.lineTo(body.right() + Tail - 1, y);
    tail.lineTo(body.right() - 1, y + 8);
  } else if (hasTail && side == Right && d->m_anchor.x() < body.left()) {
    tail.moveTo(body.left() + 1, y - 8);
    tail.lineTo(body.left() - Tail + 1, y);
    tail.lineTo(body.left() + 1, y + 8);
  }
  tail.closeSubpath();
  surface = surface.united(tail);
  const bool dark = palette().color(QPalette::Window).lightness() < 128;
  const qreal dpr = devicePixelRatioF();
  const QSize pixels(qCeil(width() * dpr), qCeil(height() * dpr));
  if (d->m_shadow.size() != pixels || d->m_shadow.devicePixelRatio() != dpr ||
      d->m_shadowSurface != surface || d->m_shadowDark != dark) {
    // 只缓存轮廓阴影，文本和子控件正常绘制；不使用全控件离屏模糊。
    d->m_shadow = QPixmap(pixels);
    d->m_shadow.setDevicePixelRatio(dpr);
    d->m_shadow.fill(Qt::transparent);
    QPainter shadowPainter(&d->m_shadow);
    shadowPainter.setRenderHint(QPainter::Antialiasing);
    // 与 FluentUI3Style::drawPopupShadow（QToolTip）保持相同的四层阴影参数。
    // 将矩形外扩改为完整轮廓外扩，让尾巴也有阴影；不改变布局预留区。
    constexpr int elevation = 5;
    constexpr int levels = elevation - 1;
    shadowPainter.setPen(Qt::NoPen);
    shadowPainter.setBrush(dark ? QColor(0, 0, 0) : QColor(0x99, 0x99, 0x99));
    QPainterPathStroker stroker;
    stroker.setJoinStyle(Qt::RoundJoin);
    for (int level = 1; level <= levels; ++level) {
      const qreal extent = qreal(FlyoutShadowBorderWidth) * level / levels;
      stroker.setWidth(2 * extent);
      const QPainterPath ring =
          stroker.createStroke(surface).subtracted(surface);
      shadowPainter.setOpacity(0.01 * (elevation - level + 1));
      shadowPainter.drawPath(ring);
    }
    d->m_shadowSurface = surface;
    d->m_shadowDark = dark;
  }
  painter.drawPixmap(0, 0, d->m_shadow);
  const QColor borderColor = WINUI3Colors[dark ? 1 : 0][controlStrokeSecondary];
  const qreal borderWidth = 1.0;
  painter.setPen(QPen(borderColor, borderWidth));
  painter.setBrush(palette().color(QPalette::Base));
  painter.drawPath(surface);
}
