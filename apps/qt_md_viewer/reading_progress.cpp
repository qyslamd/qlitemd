#include "reading_progress.h"

#include <QPainter>

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
