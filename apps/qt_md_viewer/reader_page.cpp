#include "reader_page.h"

#include <QLabel>
#include <QResizeEvent>
#include <QToolButton>
#include <QVBoxLayout>

#include "reading_progress.h"

ReaderPage::ReaderPage(QWidget* content, QWidget* parent) : QWidget(parent) {
  setObjectName(QStringLiteral("readerPage"));
  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);

  progress_ = new ReadingProgressBar(this);
  layout->addWidget(progress_);

  breadcrumb_ = new QLabel(this);
  breadcrumb_->setObjectName(QStringLiteral("readingBreadcrumb"));
  breadcrumb_->setContentsMargins(16, 6, 16, 6);
  breadcrumb_->setForegroundRole(QPalette::PlaceholderText);
  breadcrumb_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
  breadcrumb_->setTextInteractionFlags(Qt::NoTextInteraction);
  QFont crumb_font = font();
  if (crumb_font.pointSizeF() > 0.0) {
    crumb_font.setPointSizeF(qMax(8.0, crumb_font.pointSizeF() - 1.0));
  }
  breadcrumb_->setFont(crumb_font);
  breadcrumb_->hide();
  layout->addWidget(breadcrumb_);

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
