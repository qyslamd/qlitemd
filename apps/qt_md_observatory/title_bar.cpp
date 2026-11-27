#include "title_bar.h"

#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QPaintEvent>
#include <QPainter>
#include <QPainterPath>

TitleBarButton::TitleBarButton(Icon icon, QWidget* parent)
    : QAbstractButton(parent), icon_(icon) {
  setCursor(Qt::PointingHandCursor);
  setFocusPolicy(Qt::NoFocus);
  setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
}

void TitleBarButton::SetIcon(Icon icon) {
  if (icon_ == icon) {
    return;
  }
  icon_ = icon;
  update();
}

QSize TitleBarButton::sizeHint() const { return QSize(46, 32); }

QSize TitleBarButton::minimumSizeHint() const { return sizeHint(); }

void TitleBarButton::paintEvent(QPaintEvent* event) {
  (void)event;
  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing, true);

  const bool close = icon_ == Icon::kClose;
  if (underMouse() || isDown()) {
    QColor background =
        close ? QColor(196, 43, 28) : palette().color(QPalette::Highlight);
    if (!close) {
      background.setAlpha(isDown() ? 60 : 36);
    }
    painter.fillRect(rect(), background);
  }

  QColor foreground = palette().color(QPalette::WindowText);
  if (close && underMouse()) {
    foreground = QColor(255, 255, 255);
  }
  QPen pen(foreground, 1.2);
  pen.setCapStyle(Qt::RoundCap);
  pen.setJoinStyle(Qt::RoundJoin);
  painter.setPen(pen);
  painter.setBrush(Qt::NoBrush);

  const QPointF c = QRectF(rect()).center();
  switch (icon_) {
    case Icon::kMinimize:
      painter.drawLine(QPointF(c.x() - 5, c.y()), QPointF(c.x() + 5, c.y()));
      break;
    case Icon::kMaximize:
      painter.drawRect(QRectF(c.x() - 5, c.y() - 5, 10, 10));
      break;
    case Icon::kRestore:
      painter.drawRect(QRectF(c.x() - 6, c.y() - 4, 9, 9));
      painter.drawRect(QRectF(c.x() - 3, c.y() - 7, 9, 9));
      break;
    case Icon::kClose:
      painter.drawLine(QPointF(c.x() - 5, c.y() - 5),
                       QPointF(c.x() + 5, c.y() + 5));
      painter.drawLine(QPointF(c.x() - 5, c.y() + 5),
                       QPointF(c.x() + 5, c.y() - 5));
      break;
    case Icon::kTheme: {
      const QRectF circle(c.x() - 5.5, c.y() - 5.5, 11, 11);
      painter.drawEllipse(circle);
      QPainterPath half;
      half.moveTo(c.x(), circle.top());
      half.arcTo(circle, 90, -180);
      half.closeSubpath();
      painter.fillPath(half, foreground);
      break;
    }
  }
}

bool TitleBarButton::event(QEvent* event) {
  if (event->type() == QEvent::Enter || event->type() == QEvent::Leave ||
      event->type() == QEvent::MouseButtonPress ||
      event->type() == QEvent::MouseButtonRelease) {
    update();
  }
  return QAbstractButton::event(event);
}

TitleBar::TitleBar(QWidget* parent) : QWidget(parent) {
  setObjectName(QStringLiteral("titleBar"));
  setFixedHeight(40);
  // QWindowKit moves the title bar into the non-client host; style sheet
  // cascading from the window is not reliable there, so paint the surface
  // from the palette and keep the labels on the same palette.
  setAutoFillBackground(false);

  auto* layout = new QHBoxLayout(this);
  layout->setContentsMargins(14, 0, 0, 0);
  layout->setSpacing(8);

  auto* title = new QLabel(tr("OBSERVATORY"), this);
  title->setObjectName(QStringLiteral("appTitle"));
  layout->addWidget(title);

  root_label_ = new QLabel(this);
  root_label_->setObjectName(QStringLiteral("rootLabel"));
  root_label_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
  root_label_->setMinimumWidth(0);
  layout->addSpacing(10);
  layout->addWidget(root_label_, 1);

  document_label_ = new QLabel(this);
  document_label_->setObjectName(QStringLiteral("docIndex"));
  layout->addWidget(document_label_);
  layout->addSpacing(6);

  theme_button_ = new TitleBarButton(TitleBarButton::Icon::kTheme, this);
  theme_button_->setToolTip(tr("Toggle theme"));
  min_button_ = new TitleBarButton(TitleBarButton::Icon::kMinimize, this);
  min_button_->setToolTip(tr("Minimize"));
  max_button_ = new TitleBarButton(TitleBarButton::Icon::kMaximize, this);
  max_button_->setToolTip(tr("Maximize"));
  close_button_ = new TitleBarButton(TitleBarButton::Icon::kClose, this);
  close_button_->setToolTip(tr("Close"));

  layout->addWidget(theme_button_);
  layout->addWidget(min_button_);
  layout->addWidget(max_button_);
  layout->addWidget(close_button_);

  connect(theme_button_, &QAbstractButton::clicked, this,
          &TitleBar::themeToggled);
  connect(min_button_, &QAbstractButton::clicked, this, [this] {
    if (window() != nullptr) {
      window()->showMinimized();
    }
  });
  connect(max_button_, &QAbstractButton::clicked, this, [this] {
    if (window() == nullptr) {
      return;
    }
    if (window()->isMaximized()) {
      window()->showNormal();
    } else {
      window()->showMaximized();
    }
  });
  connect(close_button_, &QAbstractButton::clicked, this, [this] {
    if (window() != nullptr) {
      window()->close();
    }
  });
}

void TitleBar::SetRootText(const QString& text) {
  root_label_->setText(text);
  root_label_->setToolTip(text);
}

void TitleBar::SetDocumentText(const QString& text) {
  document_label_->setText(text);
}

void TitleBar::SetMaximized(bool maximized) {
  max_button_->SetIcon(maximized ? TitleBarButton::Icon::kRestore
                                 : TitleBarButton::Icon::kMaximize);
}

bool TitleBar::event(QEvent* event) { return QWidget::event(event); }

void TitleBar::paintEvent(QPaintEvent* event) {
  (void)event;
  QPainter painter(this);
  painter.fillRect(rect(), palette().color(QPalette::Window));
  painter.setPen(palette().color(QPalette::Mid));
  painter.drawLine(0, height() - 1, width(), height() - 1);
}
