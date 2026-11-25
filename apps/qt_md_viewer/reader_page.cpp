#include "reader_page.h"

#include <QPainter>
#include <QResizeEvent>
#include <QToolButton>
#include <QVBoxLayout>

ReadingProgressBar::ReadingProgressBar(QWidget* parent) : QWidget(parent) {
  setObjectName(QStringLiteral("readingProgressBar"));
  setFixedHeight(3);
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  setAccessibleName(QStringLiteral("阅读进度"));
}

void ReadingProgressBar::SetProgress(qreal progress) {
  const qreal clamped = qBound(0.0, progress, 1.0);
  if (qFuzzyCompare(progress_ + 1.0, clamped + 1.0)) {
    return;
  }
  progress_ = clamped;
  update();
}

void ReadingProgressBar::paintEvent(QPaintEvent* event) {
  Q_UNUSED(event);
  QPainter painter(this);
  QColor track = palette().color(QPalette::Mid);
  track.setAlpha(45);
  painter.fillRect(rect(), track);
  if (progress_ <= 0.0) {
    return;
  }
  const int filled = qRound(width() * progress_);
  painter.fillRect(QRect(0, 0, filled, height()),
                   palette().color(QPalette::Highlight));
}

ReaderPage::ReaderPage(QWidget* content, QWidget* parent) : QWidget(parent) {
  setObjectName(QStringLiteral("readerPage"));
  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);

  progress_ = new ReadingProgressBar(this);
  layout->addWidget(progress_);

  layout->addWidget(content, 1);

  // 浮动回顶：不占布局，按页面尺寸手动定位
  back_to_top_ = new QToolButton(this);
  back_to_top_->setObjectName(QStringLiteral("backToTopButton"));
  back_to_top_->setText(QStringLiteral("↑"));
  back_to_top_->setToolTip(QStringLiteral("回到顶部"));
  back_to_top_->setCursor(Qt::PointingHandCursor);
  back_to_top_->setFixedSize(38, 38);
  back_to_top_->hide();
}

void ReaderPage::resizeEvent(QResizeEvent* event) {
  QWidget::resizeEvent(event);
  PositionBackToTop();
}

void ReaderPage::PositionBackToTop() {
  constexpr int kMargin = 22;
  back_to_top_->move(width() - back_to_top_->width() - kMargin,
                     height() - back_to_top_->height() - kMargin);
  back_to_top_->raise();
}
