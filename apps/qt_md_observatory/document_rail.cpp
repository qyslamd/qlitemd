#include "document_rail.h"

#include <functional>

#include <QAbstractButton>
#include <QColor>
#include <QFont>
#include <QFontMetrics>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QPainterPath>
#include <QScrollArea>
#include <QToolButton>
#include <QVBoxLayout>

namespace {

const QColor kAccent(217, 119, 87);

QColor GroupColor(int group) {
  return QColor(BlockGroupColorHex(static_cast<BlockGroup>(group)));
}

void PaintComposition(QPainter* painter, const QRectF& rect,
                      const QVector<int>& counts, int total) {
  painter->save();
  QPainterPath clip;
  clip.addRoundedRect(rect, 3.0, 3.0);
  painter->setClipPath(clip);
  painter->fillRect(rect, QColor(255, 255, 255, 18));
  if (total > 0) {
    qreal x = rect.left();
    const qreal right = rect.right();
    for (int group = 0; group < counts.size(); ++group) {
      const int count = counts[group];
      if (count <= 0) {
        continue;
      }
      qreal width = rect.width() * static_cast<qreal>(count) / total;
      width = qMax<qreal>(2.0, width);
      if (x + width > right) {
        width = right - x;
      }
      painter->fillRect(QRectF(x, rect.top(), width, rect.height()),
                        GroupColor(group));
      x += width;
      if (x >= right) {
        break;
      }
    }
  }
  painter->restore();
}

}  // namespace

class ProgressBar : public QWidget {
 public:
  explicit ProgressBar(QWidget* parent = nullptr) : QWidget(parent) {
    setFixedHeight(4);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  }

  void SetValue(int value) {
    value_ = qBound(0, value, 100);
    update();
  }

 protected:
  void paintEvent(QPaintEvent* event) override {
    (void)event;
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.fillRect(rect(), QColor(255, 255, 255, 20));
    if (value_ > 0) {
      const int filled_width = qMax(2, width() * value_ / 100);
      painter.fillRect(QRect(0, 0, filled_width, height()), kAccent);
    }
  }

 private:
  int value_ = 0;
};

class CompositionBar : public QWidget {
 public:
  explicit CompositionBar(QWidget* parent = nullptr) : QWidget(parent) {
    setFixedHeight(10);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  }

  void SetCounts(const QVector<int>& counts, int total) {
    counts_ = counts;
    total_ = total;
    update();
  }

 protected:
  void paintEvent(QPaintEvent* event) override {
    (void)event;
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    PaintComposition(&painter, QRectF(rect()), counts_, total_);
  }

 private:
  QVector<int> counts_;
  int total_ = 0;
};

class SectionRow : public QAbstractButton {
 public:
  SectionRow(const InstrumentSection& section,
             std::function<void(const QString&)> on_click, QWidget* parent)
      : QAbstractButton(parent),
        section_(section),
        on_click_(std::move(on_click)) {
    setText(section_.title);
    setAccessibleName(section_.title);
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::StrongFocus);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setToolTip(section_.title);
    connect(this, &QAbstractButton::clicked, this, [this] {
      if (on_click_) {
        on_click_(section_.anchor);
      }
    });
  }

  void SetCurrent(bool current) {
    if (current_ == current) {
      return;
    }
    current_ = current;
    update();
  }

  QSize sizeHint() const override { return QSize(220, 42); }

 protected:
  void paintEvent(QPaintEvent* event) override {
    (void)event;
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QPalette& pal = palette();
    if (current_) {
      QColor highlight = kAccent;
      highlight.setAlpha(36);
      painter.fillRect(rect(), highlight);
      painter.fillRect(QRectF(0, 8, 2, height() - 16), kAccent);
    } else if (underMouse()) {
      painter.fillRect(rect(), pal.color(QPalette::AlternateBase));
    }

    const int indent = qMax(0, section_.level - 1) * 10 + 10;
    QFont title_font = font();
    title_font.setBold(current_);
    title_font.setPointSizeF(qMax(8.0, title_font.pointSizeF() - 0.5));
    painter.setFont(title_font);
    painter.setPen(pal.color(QPalette::WindowText));
    const QFontMetrics metrics(title_font);
    const QString count_text = QString::number(section_.block_count);
    const int count_width = metrics.horizontalAdvance(count_text) + 12;
    const int title_width = qMax(10, width() - indent - count_width - 8);
    const QString title =
        metrics.elidedText(section_.title, Qt::ElideRight, title_width);
    painter.drawText(QRect(indent, 4, title_width, 20),
                     Qt::AlignLeft | Qt::AlignVCenter, title);

    painter.setPen(pal.color(QPalette::Disabled, QPalette::WindowText));
    painter.drawText(QRect(width() - count_width, 4, count_width - 8, 20),
                     Qt::AlignRight | Qt::AlignVCenter, count_text);

    const QRectF mini(indent, height() - 12, width() - indent - 10, 3);
    PaintComposition(&painter, mini, section_.group_counts,
                     section_.block_count);

    if (hasFocus()) {
      QColor focus = kAccent;
      focus.setAlpha(160);
      painter.setPen(focus);
      painter.drawRect(rect().adjusted(1, 1, -2, -2));
    }

    QColor separator = pal.color(QPalette::Mid);
    separator.setAlpha(70);
    painter.setPen(separator);
    painter.drawLine(0, height() - 1, width(), height() - 1);
  }

 private:
  InstrumentSection section_;
  std::function<void(const QString&)> on_click_;
  bool current_ = false;
};

class SpineWidget : public QWidget {
 public:
  explicit SpineWidget(QWidget* parent = nullptr) : QWidget(parent) {
    layout_ = new QVBoxLayout(this);
    layout_->setContentsMargins(0, 0, 0, 0);
    layout_->setSpacing(1);
    setAutoFillBackground(false);
  }

  void SetSections(const QVector<InstrumentSection>& sections,
                   std::function<void(const QString&)> on_click) {
    while (QLayoutItem* item = layout_->takeAt(0)) {
      if (item->widget() != nullptr) {
        delete item->widget();
      }
      delete item;
    }
    rows_.clear();
    for (const InstrumentSection& section : sections) {
      auto* row = new SectionRow(section, on_click, this);
      layout_->addWidget(row);
      rows_.push_back(row);
    }
    layout_->addStretch(1);
  }

  void SetCurrent(int index) {
    for (int i = 0; i < rows_.size(); ++i) {
      rows_[i]->SetCurrent(i == index);
    }
  }

 private:
  QVBoxLayout* layout_ = nullptr;
  QVector<SectionRow*> rows_;
};

DocumentRail::DocumentRail(QWidget* parent) : QWidget(parent) {
  setObjectName(QStringLiteral("railPanel"));
  setMinimumWidth(250);

  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(16, 14, 16, 14);
  layout->setSpacing(6);

  auto* title = new QLabel(tr("DOCUMENT"), this);
  title->setObjectName(QStringLiteral("panelTitle"));
  layout->addWidget(title);

  document_title_ = new QLabel(this);
  document_title_->setObjectName(QStringLiteral("railDocTitle"));
  document_title_->setWordWrap(true);
  QFont title_font = document_title_->font();
  title_font.setPointSizeF(title_font.pointSizeF() + 4);
  title_font.setWeight(QFont::DemiBold);
  document_title_->setFont(title_font);
  layout->addWidget(document_title_);
  layout->addSpacing(10);

  auto* progress_title = new QLabel(tr("PROGRESS"), this);
  progress_title->setObjectName(QStringLiteral("panelTitle"));
  layout->addWidget(progress_title);
  percent_label_ = new QLabel(this);
  percent_label_->setObjectName(QStringLiteral("railPercent"));
  layout->addWidget(percent_label_);
  progress_bar_ = new ProgressBar(this);
  progress_bar_->setObjectName(QStringLiteral("railProgress"));
  layout->addWidget(progress_bar_);

  layout->addSpacing(12);
  auto* composition_title = new QLabel(tr("COMPOSITION"), this);
  composition_title->setObjectName(QStringLiteral("panelTitle"));
  layout->addWidget(composition_title);
  composition_bar_ = new CompositionBar(this);
  layout->addWidget(composition_bar_);
  legend_layout_ = new QVBoxLayout();
  legend_layout_->setContentsMargins(0, 2, 0, 0);
  legend_layout_->setSpacing(2);
  layout->addLayout(legend_layout_);

  layout->addSpacing(12);
  spine_toggle_ = new QToolButton(this);
  spine_toggle_->setObjectName(QStringLiteral("panelTitleButton"));
  spine_toggle_->setText(tr("SPINE  v"));
  spine_toggle_->setCheckable(true);
  spine_toggle_->setChecked(true);
  spine_toggle_->setCursor(Qt::PointingHandCursor);
  spine_toggle_->setToolButtonStyle(Qt::ToolButtonTextOnly);
  layout->addWidget(spine_toggle_, 0, Qt::AlignLeft);

  spine_ = new SpineWidget(this);
  spine_scroll_ = new QScrollArea(this);
  spine_scroll_->setWidgetResizable(true);
  spine_scroll_->setFrameShape(QFrame::NoFrame);
  spine_scroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  spine_scroll_->setWidget(spine_);
  spine_scroll_->viewport()->setAutoFillBackground(false);
  layout->addWidget(spine_scroll_, 1);

  // When the spine is collapsed, this invisible tail keeps the vertical
  // stretch so the labels above do not spread over the whole rail.
  spine_tail_ = new QWidget(this);
  spine_tail_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
  layout->addWidget(spine_tail_, 1);
  spine_tail_->hide();

  connect(spine_toggle_, &QToolButton::toggled, this, [this](bool open) {
    spine_scroll_->setVisible(open);
    spine_tail_->setVisible(!open);
    spine_toggle_->setText(open ? tr("SPINE  v") : tr("SPINE  >"));
  });

  empty_label_ = new QLabel(tr("No document"), this);
  empty_label_->setObjectName(QStringLiteral("muted"));
  layout->addWidget(empty_label_);
  empty_label_->hide();

  Clear();
}

void DocumentRail::Clear() {
  data_ = InstrumentData();
  current_section_ = -1;
  scroll_ = 0;
  document_height_ = 0;
  viewport_height_ = 0;
  document_title_->clear();
  percent_label_->setText(QStringLiteral("0%"));
  progress_bar_->SetValue(0);
  composition_bar_->SetCounts({}, 0);
  spine_->SetSections({}, {});
  while (QLayoutItem* item = legend_layout_->takeAt(0)) {
    if (item->widget() != nullptr) {
      delete item->widget();
    }
    delete item;
  }
  empty_label_->show();
}

void DocumentRail::SetDocument(const QString& title,
                               const QVector<MarkdownBlock>& blocks,
                               const QVector<MarkdownHeading>& outline,
                               const QVector<int>& heading_positions) {
  document_title_->setText(title);
  empty_label_->setVisible(blocks.isEmpty() && outline.isEmpty());
  data_ = BuildInstrumentData(blocks, outline, heading_positions, scroll_,
                              document_height_, viewport_height_);
  composition_bar_->SetCounts(data_.composition, data_.total_blocks);
  spine_->SetSections(data_.sections, [this](const QString& anchor) {
    emit anchorActivated(anchor);
  });
  RebuildLegend();
  UpdateCurrentSection();
  percent_label_->setText(QStringLiteral("%1%").arg(data_.progress));
  progress_bar_->SetValue(data_.progress);
}

void DocumentRail::ToggleSpine() {
  if (spine_toggle_ != nullptr) {
    spine_toggle_->click();
  }
}

void DocumentRail::SetScroll(int scroll, int document_height,
                             int viewport_height) {
  scroll_ = qMax(0, scroll);
  document_height_ = document_height;
  viewport_height_ = viewport_height;
  const int progress =
      ComputeProgress(scroll_, document_height_, viewport_height_);
  percent_label_->setText(QStringLiteral("%1%").arg(progress));
  progress_bar_->SetValue(progress);
  UpdateCurrentSection();
}

void DocumentRail::UpdateCurrentSection() {
  int current = -1;
  for (int i = 0; i < data_.sections.size(); ++i) {
    if (data_.sections[i].top >= 0 && data_.sections[i].top <= scroll_ + 24) {
      current = i;
    }
  }
  if (current == current_section_) {
    return;
  }
  current_section_ = current;
  spine_->SetCurrent(current);
}

void DocumentRail::RebuildLegend() {
  while (QLayoutItem* item = legend_layout_->takeAt(0)) {
    if (item->widget() != nullptr) {
      delete item->widget();
    }
    delete item;
  }

  int rows = 0;
  for (int group = 0; group < data_.composition.size(); ++group) {
    const int count = data_.composition[group];
    if (count <= 0) {
      continue;
    }
    auto* row = new QWidget(this);
    auto* row_layout = new QHBoxLayout(row);
    row_layout->setContentsMargins(0, 0, 0, 0);
    row_layout->setSpacing(6);
    auto* swatch = new QLabel(row);
    swatch->setFixedSize(8, 8);
    swatch->setStyleSheet(
        QStringLiteral("background:%1;border-radius:2px;")
            .arg(BlockGroupColorHex(static_cast<BlockGroup>(group))));
    QString label_text;
    switch (static_cast<BlockGroup>(group)) {
      case BlockGroup::kHeading:
        label_text = tr("Headings");
        break;
      case BlockGroup::kText:
        label_text = tr("Text");
        break;
      case BlockGroup::kCode:
        label_text = tr("Code");
        break;
      case BlockGroup::kMath:
        label_text = tr("Math");
        break;
      case BlockGroup::kDiagram:
        label_text = tr("Diagrams");
        break;
      case BlockGroup::kTable:
        label_text = tr("Tables");
        break;
      case BlockGroup::kNote:
        label_text = tr("Notes");
        break;
      case BlockGroup::kRule:
        label_text = tr("Rules");
        break;
    }
    auto* label = new QLabel(label_text, row);
    label->setObjectName(QStringLiteral("muted"));
    auto* value = new QLabel(QString::number(count), row);
    value->setObjectName(QStringLiteral("muted"));
    row_layout->addWidget(swatch);
    row_layout->addWidget(label);
    row_layout->addStretch(1);
    row_layout->addWidget(value);
    legend_layout_->addWidget(row);
    ++rows;
  }

  if (rows == 0) {
    auto* hint = new QLabel(tr("No blocks"), this);
    hint->setObjectName(QStringLiteral("muted"));
    legend_layout_->addWidget(hint);
  }
}
