#include "render_status_panel.h"

// clang-format off
#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#else
#include <QFile>
#endif
// clang-format on

#include <exexpander.h>
#include <exprogressring.h>

#include <algorithm>
#include <limits>

#include <QCoreApplication>
#include <QFont>
#include <QFontMetrics>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPaintEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSizePolicy>
#include <QStringList>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>
#include <QtMath>

#include "markdown_render_context.h"
#include "markdown_types.h"
#include "markdown_view.h"

namespace {

constexpr int kStageCount = 6;

QString OnOff(bool enabled) {
  return enabled ? QCoreApplication::translate("RenderStatusPanel", "On")
                 : QCoreApplication::translate("RenderStatusPanel", "Off");
}

QString BytesText(qint64 bytes) {
  if (bytes >= 1024LL * 1024 * 1024) {
    return QStringLiteral("%1 GB").arg(bytes / (1024.0 * 1024 * 1024), 0, 'f',
                                       2);
  }
  if (bytes >= 1024LL * 1024) {
    return QStringLiteral("%1 MB").arg(bytes / (1024.0 * 1024), 0, 'f', 1);
  }
  if (bytes >= 1024) {
    return QStringLiteral("%1 KB").arg(bytes / 1024.0, 0, 'f', 1);
  }
  return QStringLiteral("%1 B").arg(bytes);
}

QString ProcessMemoryText() {
#ifdef _WIN32
  PROCESS_MEMORY_COUNTERS info;
  if (!GetProcessMemoryInfo(GetCurrentProcess(), &info, sizeof(info))) {
    return QCoreApplication::translate("RenderStatusPanel", "Unavailable");
  }
  return QCoreApplication::translate("RenderStatusPanel",
                                     "Working set %1 · peak %2")
      .arg(BytesText(static_cast<qint64>(info.WorkingSetSize)),
           BytesText(static_cast<qint64>(info.PeakWorkingSetSize)));
#else
  QFile status(QStringLiteral("/proc/self/status"));
  if (!status.open(QIODevice::ReadOnly)) {
    return QCoreApplication::translate("RenderStatusPanel", "Unavailable");
  }
  const QString text = QString::fromLatin1(status.readAll());
  QString rss;
  QString peak;
  for (const QString& line : text.split(QLatin1Char('\n'))) {
    if (line.startsWith(QLatin1String("VmRSS:"))) {
      rss = line.simplified();
    } else if (line.startsWith(QLatin1String("VmHWM:"))) {
      peak = line.simplified();
    }
  }
  return rss + QStringLiteral(" · ") + peak;
#endif
}

QColor AccentColor(const QWidget* widget) {
  return widget->palette().color(QPalette::Highlight);
}

bool IsDark(const QWidget* widget) {
  return widget->palette().color(QPalette::Window).lightness() < 128;
}

QColor BadColor(const QWidget* widget) {
  return IsDark(widget) ? QColor(255, 155, 146) : QColor(179, 38, 30);
}

QColor StageColor(int index, const QWidget* widget) {
  switch (index) {
    case 0:
      return AccentColor(widget);
    case 1:
      return IsDark(widget) ? QColor(141, 200, 239) : QColor(92, 159, 212);
    case 2:
      return IsDark(widget) ? QColor(85, 197, 182) : QColor(19, 137, 125);
    case 3:
      return IsDark(widget) ? QColor(239, 191, 97) : QColor(190, 121, 0);
    case 4:
      return IsDark(widget) ? QColor(255, 155, 146) : QColor(178, 75, 50);
    default:
      return widget->palette().color(QPalette::Mid);
  }
}

void SetBarColor(QProgressBar* bar, const QColor& color) {
  QPalette palette = bar->palette();
  palette.setColor(QPalette::Highlight, color);
  bar->setPalette(palette);
}

void MakeMuted(QLabel* label) {
  label->setForegroundRole(QPalette::PlaceholderText);
}

void MakeSmall(QLabel* label, int pixel_size) {
  QFont font = label->font();
  font.setPixelSize(pixel_size);
  label->setFont(font);
}

// 圆环中心宽度固定；任务数变长时按实际字宽缩字号，避免文字被省略
int FitRingFontPixelSize(const QFont& base, const QString& text,
                         int max_width) {
  QFont font = base;
  font.setBold(true);
  for (int pixel_size = 14; pixel_size >= 8; --pixel_size) {
    font.setPixelSize(pixel_size);
    if (QFontMetrics(font).horizontalAdvance(text) <= max_width) {
      return pixel_size;
    }
  }
  return 8;
}

// 每个分组做成可折叠的抽屉；标题和右侧摘要始终可见，内容按需展开
ExExpander* NewGroup(QWidget* parent, const QString& title,
                     QVBoxLayout** content, QLabel** hint = nullptr,
                     bool expanded = false) {
  auto* group = new ExExpander(parent);
  group->setAccessibleName(title);

  auto* header = new QWidget(group);
  auto* head = new QHBoxLayout(header);
  head->setContentsMargins(0, 0, 0, 0);
  head->setSpacing(6);
  auto* title_label = new QLabel(title, header);
  QFont title_font = title_label->font();
  title_font.setBold(true);
  title_font.setPixelSize(13);
  title_label->setFont(title_font);
  head->addWidget(title_label);
  head->addStretch(1);
  if (hint != nullptr) {
    *hint = new QLabel(header);
    MakeMuted(*hint);
    MakeSmall(*hint, 10);
    head->addWidget(*hint);
  }
  group->setHeaderWidget(header);

  auto* body = new QWidget;
  auto* layout = new QVBoxLayout(body);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(6);
  group->addContentWidget(body);
  group->setExpanded(expanded);

  *content = layout;
  return group;
}

void AddDivider(QVBoxLayout* layout) {
  auto* line = new QFrame;
  line->setFrameShape(QFrame::HLine);
  line->setFrameShadow(QFrame::Plain);
  layout->addWidget(line);
}

void AddDetailRow(QGridLayout* layout, int row, const QString& key,
                  QLabel** value) {
  auto* key_label = new QLabel(key);
  MakeMuted(key_label);
  MakeSmall(key_label, 10);
  layout->addWidget(key_label, row, 0, Qt::AlignTop);

  *value = new QLabel(QCoreApplication::translate("RenderStatusPanel", "—"));
  (*value)->setWordWrap(true);
  (*value)->setAlignment(Qt::AlignRight | Qt::AlignTop);
  MakeSmall(*value, 10);
  layout->addWidget(*value, row, 1);
}

}  // namespace

class WorkerPips final : public QWidget {
 public:
  explicit WorkerPips(QWidget* parent = nullptr) : QWidget(parent) {
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
  }

  void SetStatus(int workers, int alive, int failed, int processing) {
    workers_ = qMax(0, workers);
    alive_ = qBound(0, alive, workers_);
    failed_ = qBound(0, failed, workers_);
    processing_ = qBound(0, processing, alive_);
    setToolTip(tr("workers %1 · alive %2 · busy %3 · failed %4")
                   .arg(workers_)
                   .arg(alive_)
                   .arg(processing_)
                   .arg(failed_));
    updateGeometry();
    update();
  }

  QSize sizeHint() const override {
    constexpr int kDiameter = 10;
    constexpr int kGap = 4;
    const int count = qMax(1, workers_);
    const int width = (count * kDiameter) + ((count - 1) * kGap);
    return QSize(width, kDiameter);
  }

 protected:
  void paintEvent(QPaintEvent* event) override {
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    constexpr qreal kDiameter = 10.0;
    constexpr qreal kGap = 4.0;
    const qreal y = (height() - kDiameter) / 2.0;
    const QColor accent = AccentColor(this);
    const QColor mid = palette().color(QPalette::Mid);
    for (int i = 0; i < workers_; ++i) {
      const qreal x = i * (kDiameter + kGap);
      const QRectF circle(x, y, kDiameter, kDiameter);
      if (i < alive_) {
        if (i < processing_) {
          painter.setPen(Qt::NoPen);
          painter.setBrush(accent);
          painter.drawEllipse(circle);
        } else {
          painter.setBrush(Qt::NoBrush);
          painter.setPen(QPen(mid, 1.4));
          painter.drawEllipse(circle.adjusted(0.7, 0.7, -0.7, -0.7));
        }
      } else {
        painter.setPen(Qt::NoPen);
        painter.setBrush(BadColor(this));
        painter.drawEllipse(circle);
        painter.setPen(QPen(palette().color(QPalette::Window), 1.3));
        painter.drawLine(QPointF(circle.left() + 2.8, circle.top() + 2.8),
                         QPointF(circle.right() - 2.8, circle.bottom() - 2.8));
        painter.drawLine(QPointF(circle.right() - 2.8, circle.top() + 2.8),
                         QPointF(circle.left() + 2.8, circle.bottom() - 2.8));
      }
    }
  }

 private:
  int workers_ = 0;
  int alive_ = 0;
  int failed_ = 0;
  int processing_ = 0;
};

class HitBar final : public QWidget {
  // tooltip 用了 tr()：缺 Q_OBJECT 会把上下文错成 QObject，翻译表对不上
  Q_OBJECT

 public:
  explicit HitBar(QWidget* parent = nullptr) : QWidget(parent) {
    setMinimumHeight(8);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  }

  void SetCounts(qint64 rendered, qint64 memory_hits, qint64 disk_hits) {
    rendered_ = qMax<qint64>(0, rendered);
    memory_hits_ = qMax<qint64>(0, memory_hits);
    disk_hits_ = qMax<qint64>(0, disk_hits);
    setToolTip(tr("renders %1 · memory hits %2 · disk hits %3")
                   .arg(rendered_)
                   .arg(memory_hits_)
                   .arg(disk_hits_));
    update();
  }

  QSize sizeHint() const override { return QSize(100, 8); }

 protected:
  void paintEvent(QPaintEvent* event) override {
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QRectF bar = QRectF(rect()).adjusted(0.0, 0.5, -0.5, -0.5);
    const qreal radius = qMin<qreal>(3.0, bar.height() / 2.0);
    QPainterPath clip;
    clip.addRoundedRect(bar, radius, radius);
    painter.setClipPath(clip);

    QColor track = palette().color(QPalette::Mid);
    track.setAlpha(70);
    painter.fillRect(bar, track);

    const qint64 total = rendered_ + memory_hits_ + disk_hits_;
    if (total <= 0) {
      return;
    }
    qreal x = bar.left();
    const auto draw_segment = [&](qint64 value, const QColor& color) {
      if (value <= 0) {
        return;
      }
      const qreal width = bar.width() * value / total;
      painter.fillRect(QRectF(x, bar.top(), width, bar.height()), color);
      x += width;
    };
    draw_segment(rendered_, AccentColor(this));
    draw_segment(memory_hits_, StageColor(2, this));
    draw_segment(disk_hits_, StageColor(3, this));
  }

 private:
  qint64 rendered_ = 0;
  qint64 memory_hits_ = 0;
  qint64 disk_hits_ = 0;
};

class StatusPill final : public QWidget {
 public:
  enum class Tone {
    kGood,
    kMuted,
    kBad,
  };

  explicit StatusPill(QWidget* parent = nullptr) : QWidget(parent) {
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
  }

  void SetStatus(const QString& text, Tone tone) {
    if (text_ == text && tone_ == tone) {
      return;
    }
    text_ = text;
    tone_ = tone;
    updateGeometry();
    update();
  }

  QSize sizeHint() const override {
    const QFontMetrics metrics(font());
    return QSize(metrics.horizontalAdvance(text_) + 12,
                 qMax(18, metrics.height() + 4));
  }

 protected:
  void paintEvent(QPaintEvent* event) override {
    Q_UNUSED(event);
    QColor base;
    if (tone_ == Tone::kGood) {
      base = AccentColor(this);
    } else if (tone_ == Tone::kBad) {
      base = BadColor(this);
    } else {
      base = palette().color(QPalette::PlaceholderText);
    }
    QColor background = base;
    background.setAlpha(IsDark(this) ? 44 : 32);
    QColor border = base;
    border.setAlpha(110);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QRectF pill = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    painter.setPen(QPen(border, 1.0));
    painter.setBrush(background);
    painter.drawRoundedRect(pill, pill.height() / 2.0, pill.height() / 2.0);
    painter.setPen(base);
    painter.setFont(font());
    painter.drawText(rect(), Qt::AlignCenter, text_);
  }

 private:
  QString text_;
  Tone tone_ = Tone::kMuted;
};

RenderStatusPanel::RenderStatusPanel(MarkdownView* view, QWidget* parent)
    : QWidget(parent), view_(view) {
  setObjectName(QStringLiteral("renderStatusPanel"));
  initUi();
  initSignals();
  RefreshStatus();
}

void RenderStatusPanel::initUi() {
  // 侧栏可以被压得很矮：内容放进滚动区，别把计数器挤没
  auto* panel_layout = new QVBoxLayout(this);
  panel_layout->setContentsMargins(0, 0, 0, 0);

  auto* scroll = new QScrollArea(this);
  scroll->setObjectName(QStringLiteral("statusScroll"));
  scroll->setWidgetResizable(true);
  scroll->setFrameShape(QFrame::NoFrame);
  scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  // blur 模式下侧栏本身是半透明的，滚动区不能再自己刷一层底色
  scroll->setAutoFillBackground(false);
  scroll->setBackgroundRole(QPalette::NoRole);
  scroll->viewport()->setAutoFillBackground(false);
  scroll->viewport()->setBackgroundRole(QPalette::NoRole);

  auto* body = new QWidget;
  body->setAutoFillBackground(false);
  body->setBackgroundRole(QPalette::NoRole);
  scroll_body_ = body;
  auto* layout = new QVBoxLayout(body);
  scroll_body_layout_ = layout;
  layout->setContentsMargins(8, 8, 8, 8);
  layout->setSpacing(8);

  auto* actions = new QWidget(body);
  actions->setObjectName(QStringLiteral("statusActions"));
  auto* actions_layout = new QHBoxLayout(actions);
  actions_layout->setContentsMargins(0, 0, 0, 0);
  actions_layout->setSpacing(6);
  refresh_button_ = new QPushButton(tr("Refresh"), actions);
  reset_button_ = new QPushButton(tr("Reset counters"), actions);
  actions_layout->addWidget(refresh_button_);
  actions_layout->addWidget(reset_button_);
  actions_layout->addStretch(1);
  layout->addWidget(actions);

  QVBoxLayout* progress_layout = nullptr;
  auto* progress_card = NewGroup(body, tr("Task Progress"), &progress_layout,
                                 &progress_hint_, true);
  progress_card->setObjectName(QStringLiteral("statusProgressGroup"));
  TrackGroup(progress_card);
  auto* progress_row = new QWidget(progress_card);
  auto* progress_row_layout = new QHBoxLayout(progress_row);
  progress_row_layout->setContentsMargins(0, 0, 0, 0);
  progress_row_layout->setSpacing(10);
  progress_ring_ = new ExProgressRing(progress_row);
  progress_ring_->setFixedSize(72, 72);
  progress_ring_->setRange(0, 1);
  progress_ring_->setValue(0);
  progress_ring_->setTextVisible(true);
  progress_ring_->setFormat(tr("—"));
  QFont ring_font = progress_ring_->font();
  ring_font.setPixelSize(14);
  ring_font.setBold(true);
  progress_ring_->setValueFont(ring_font);
  progress_row_layout->addWidget(progress_ring_, 0, Qt::AlignTop);

  auto* progress_copy = new QWidget(progress_row);
  auto* progress_copy_layout = new QVBoxLayout(progress_copy);
  progress_copy_layout->setContentsMargins(0, 0, 0, 0);
  progress_copy_layout->setSpacing(2);
  progress_percent_ = new QLabel(tr("—"), progress_copy);
  QFont percent_font = progress_percent_->font();
  percent_font.setPixelSize(21);
  percent_font.setBold(true);
  progress_percent_->setFont(percent_font);
  progress_meta_ = new QLabel(progress_copy);
  progress_meta_->setWordWrap(true);
  MakeMuted(progress_meta_);
  MakeSmall(progress_meta_, 10);
  progress_chips_ = new QLabel(progress_copy);
  progress_chips_->setWordWrap(true);
  MakeMuted(progress_chips_);
  MakeSmall(progress_chips_, 10);
  progress_copy_layout->addWidget(progress_percent_);
  progress_copy_layout->addWidget(progress_meta_);
  progress_copy_layout->addWidget(progress_chips_);
  progress_copy_layout->addStretch(1);
  progress_row_layout->addWidget(progress_copy, 1);
  progress_layout->addWidget(progress_row);
  layout->addWidget(progress_card);

  QVBoxLayout* timing_layout = nullptr;
  auto* timing_card =
      NewGroup(body, tr("Timing Breakdown"), &timing_layout, &timing_hint_);
  timing_card->setObjectName(QStringLiteral("statusTimingGroup"));
  TrackGroup(timing_card);
  const QStringList stage_names{
      tr("Parse"),    tr("Collect"), QStringLiteral("worker"),
      tr("Assemble"), tr("Layout"),  tr("Other")};
  for (int i = 0; i < kStageCount; ++i) {
    auto* row = new QWidget(timing_card);
    auto* row_layout = new QHBoxLayout(row);
    row_layout->setContentsMargins(0, 0, 0, 0);
    row_layout->setSpacing(6);
    auto* name = new QLabel(stage_names.at(i), row);
    name->setFixedWidth(38);
    MakeMuted(name);
    MakeSmall(name, 10);
    auto* bar = new QProgressBar(row);
    bar->setRange(0, 100);
    bar->setValue(0);
    bar->setTextVisible(false);
    bar->setFixedHeight(8);
    bar->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto* value = new QLabel(QStringLiteral("0 ms"), row);
    value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    value->setFixedWidth(48);
    MakeSmall(value, 10);
    row_layout->addWidget(name);
    row_layout->addWidget(bar, 1);
    row_layout->addWidget(value);
    timing_layout->addWidget(row);
    stage_bars_[i] = bar;
    stage_values_[i] = value;
  }
  layout->addWidget(timing_card);

  QVBoxLayout* pools_layout = nullptr;
  auto* pools_card =
      NewGroup(body, tr("Worker Pool"), &pools_layout, &pools_hint_);
  pools_card->setObjectName(QStringLiteral("statusPoolsGroup"));
  TrackGroup(pools_card);
  const QStringList pool_names{tr("Highlighting"), tr("Formulas"),
                               QStringLiteral("Mermaid")};
  for (int i = 0; i < 3; ++i) {
    if (i > 0) {
      AddDivider(pools_layout);
    }
    auto* row = new QWidget(pools_card);
    auto* row_layout = new QHBoxLayout(row);
    row_layout->setContentsMargins(0, 0, 0, 0);
    row_layout->setSpacing(7);

    auto* name_box = new QWidget(row);
    auto* name_layout = new QVBoxLayout(name_box);
    name_layout->setContentsMargins(0, 0, 0, 0);
    name_layout->setSpacing(0);
    auto* name = new QLabel(pool_names.at(i), name_box);
    QFont name_font = name->font();
    name_font.setBold(true);
    name_font.setPixelSize(11);
    name->setFont(name_font);
    auto* workers = new QLabel(name_box);
    MakeMuted(workers);
    MakeSmall(workers, 9);
    workers->setObjectName(QStringLiteral("statusPoolWorkers%1").arg(i));
    name_layout->addWidget(name);
    name_layout->addWidget(workers);
    row_layout->addWidget(name_box);

    auto* pips = new WorkerPips(row);
    pips->setFixedHeight(12);
    row_layout->addWidget(pips);
    row_layout->addStretch(1);

    auto* meta = new QLabel(row);
    meta->setWordWrap(true);
    meta->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    MakeMuted(meta);
    MakeSmall(meta, 10);
    row_layout->addWidget(meta, 0, Qt::AlignRight);

    pools_layout->addWidget(row);
    pools_[i].pips = pips;
    pools_[i].workers = workers;
    pools_[i].meta = meta;
  }
  auto* pool_legend =
      new QLabel(tr("filled=busy · hollow=idle · red=failed"), pools_card);
  MakeMuted(pool_legend);
  MakeSmall(pool_legend, 9);
  pool_legend->setWordWrap(true);
  pools_layout->addWidget(pool_legend);
  layout->addWidget(pools_card);

  QVBoxLayout* engines_layout = nullptr;
  auto* engines_card =
      NewGroup(body, tr("Engine Hits"), &engines_layout, &engines_hint_);
  engines_card->setObjectName(QStringLiteral("statusEnginesGroup"));
  TrackGroup(engines_card);
  const QStringList engine_names{tr("Highlighting"), tr("Formulas"),
                                 QStringLiteral("Mermaid")};
  const QStringList engine_object_names{QStringLiteral("statusHighlight"),
                                        QStringLiteral("statusFormula"),
                                        QStringLiteral("statusMermaid")};
  for (int i = 0; i < 3; ++i) {
    if (i > 0) {
      AddDivider(engines_layout);
    }
    auto* row = new QWidget(engines_card);
    auto* row_layout = new QVBoxLayout(row);
    row_layout->setContentsMargins(0, 0, 0, 0);
    row_layout->setSpacing(3);

    auto* top = new QWidget(row);
    auto* top_layout = new QHBoxLayout(top);
    top_layout->setContentsMargins(0, 0, 0, 0);
    top_layout->setSpacing(5);
    auto* name = new QLabel(engine_names.at(i), top);
    QFont name_font = name->font();
    name_font.setBold(true);
    name_font.setPixelSize(11);
    name->setFont(name_font);
    auto* pill = new StatusPill(top);
    auto* sub = new QLabel(top);
    MakeMuted(sub);
    MakeSmall(sub, 9);
    top_layout->addWidget(name);
    top_layout->addWidget(pill);
    top_layout->addStretch(1);
    top_layout->addWidget(sub);

    auto* bar = new HitBar(row);
    bar->setFixedHeight(8);
    auto* detail = new QLabel(row);
    detail->setObjectName(engine_object_names.at(i));
    detail->setWordWrap(true);
    MakeMuted(detail);
    MakeSmall(detail, 9);

    row_layout->addWidget(top);
    row_layout->addWidget(bar);
    row_layout->addWidget(detail);
    engines_layout->addWidget(row);
    engines_[i].pill = pill;
    engines_[i].bar = bar;
    engines_[i].sub = sub;
    engines_[i].detail = detail;
  }
  layout->addWidget(engines_card);

  QVBoxLayout* cache_layout = nullptr;
  auto* cache_card = NewGroup(body, tr("Cache"), &cache_layout, &cache_hint_);
  cache_card->setObjectName(QStringLiteral("statusCacheGroup"));
  TrackGroup(cache_card);
  auto* cache_grid = new QGridLayout;
  cache_grid->setContentsMargins(0, 0, 0, 0);
  cache_grid->setSpacing(5);
  const QStringList cache_names{tr("Highlighting"), tr("Formulas"),
                                QStringLiteral("Mermaid"), tr("Images")};
  for (int i = 0; i < 4; ++i) {
    auto* cell = new QWidget(cache_card);
    cell->setProperty("isCard", true);
    cell->setAttribute(Qt::WA_StyledBackground, true);
    auto* cell_layout = new QVBoxLayout(cell);
    cell_layout->setContentsMargins(6, 5, 6, 5);
    cell_layout->setSpacing(2);
    auto* name = new QLabel(cache_names.at(i), cell);
    MakeMuted(name);
    MakeSmall(name, 9);
    auto* value = new QLabel(tr("—"), cell);
    QFont value_font = value->font();
    value_font.setBold(true);
    value_font.setPixelSize(10);
    value->setFont(value_font);
    auto* bar = new QProgressBar(cell);
    bar->setRange(0, 1);
    bar->setValue(0);
    bar->setTextVisible(false);
    bar->setFixedHeight(5);
    cell_layout->addWidget(name);
    cell_layout->addWidget(value);
    cell_layout->addWidget(bar);
    cache_grid->addWidget(cell, i / 2, i % 2);
    cache_bars_[i] = bar;
    cache_values_[i] = value;
  }
  cache_layout->addLayout(cache_grid);

  auto* image_cap_head = new QWidget(cache_card);
  auto* image_cap_head_layout = new QHBoxLayout(image_cap_head);
  image_cap_head_layout->setContentsMargins(0, 0, 0, 0);
  image_cap_head_layout->setSpacing(6);
  auto* image_cap_title = new QLabel(tr("Image cache / limit"), image_cap_head);
  MakeMuted(image_cap_title);
  MakeSmall(image_cap_title, 9);
  image_cap_value_ = new QLabel(tr("—"), image_cap_head);
  image_cap_value_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
  MakeSmall(image_cap_value_, 9);
  image_cap_head_layout->addWidget(image_cap_title);
  image_cap_head_layout->addStretch(1);
  image_cap_head_layout->addWidget(image_cap_value_);
  cache_layout->addWidget(image_cap_head);
  image_cap_bar_ = new QProgressBar(cache_card);
  image_cap_bar_->setRange(0, 1);
  image_cap_bar_->setValue(0);
  image_cap_bar_->setTextVisible(false);
  image_cap_bar_->setFixedHeight(5);
  cache_layout->addWidget(image_cap_bar_);

  auto* details = new ExExpander(cache_card);
  details->setObjectName(QStringLiteral("statusCacheDetails"));
  details->setHeader(tr("Persistent cache and diagnostics"));
  TrackGroup(details);
  auto* detail_content = new QWidget;
  auto* detail_layout = new QGridLayout(detail_content);
  detail_layout->setContentsMargins(0, 0, 0, 0);
  detail_layout->setSpacing(4);
  AddDetailRow(detail_layout, 0, tr("Persistent Cache"), &detail_cache_);
  AddDetailRow(detail_layout, 1, tr("Entry limit"), &detail_limits_);
  AddDetailRow(detail_layout, 2, tr("Process memory"), &detail_memory_);
  AddDetailRow(detail_layout, 3, tr("Diagnostics"), &detail_diagnostics_);
  details->addContentWidget(detail_content);
  cache_layout->addWidget(details);
  layout->addWidget(cache_card);

  QVBoxLayout* doc_layout = nullptr;
  auto* doc_card = NewGroup(body, tr("Documents"), &doc_layout, &doc_hint_);
  doc_card->setObjectName(QStringLiteral("statusDocumentGroup"));
  TrackGroup(doc_card);
  auto* doc_grid = new QGridLayout;
  doc_grid->setContentsMargins(0, 0, 0, 0);
  doc_grid->setSpacing(5);
  const QStringList doc_names{tr("Headings"), tr("Code Blocks"), tr("Formulas"),
                              QStringLiteral("Mermaid")};
  for (int i = 0; i < 4; ++i) {
    auto* cell = new QWidget(doc_card);
    cell->setProperty("isCard", true);
    cell->setAttribute(Qt::WA_StyledBackground, true);
    auto* cell_layout = new QVBoxLayout(cell);
    cell_layout->setContentsMargins(6, 5, 6, 5);
    cell_layout->setSpacing(0);
    auto* name = new QLabel(doc_names.at(i), cell);
    MakeMuted(name);
    MakeSmall(name, 9);
    auto* value = new QLabel(QStringLiteral("0"), cell);
    QFont value_font = value->font();
    value_font.setBold(true);
    value_font.setPixelSize(14);
    value->setFont(value_font);
    cell_layout->addWidget(name);
    cell_layout->addWidget(value);
    doc_grid->addWidget(cell, i / 2, i % 2);
    doc_values_[i] = value;
  }
  doc_layout->addLayout(doc_grid);

  const QStringList char_names{tr("Source"), QStringLiteral("HTML")};
  for (int i = 0; i < 2; ++i) {
    auto* row = new QWidget(doc_card);
    auto* row_layout = new QHBoxLayout(row);
    row_layout->setContentsMargins(0, 0, 0, 0);
    row_layout->setSpacing(6);
    auto* name = new QLabel(char_names.at(i), row);
    name->setFixedWidth(28);
    MakeMuted(name);
    MakeSmall(name, 9);
    auto* bar = new QProgressBar(row);
    bar->setRange(0, 1);
    bar->setValue(0);
    bar->setTextVisible(false);
    bar->setFixedHeight(5);
    auto* value = new QLabel(tr("0 chars"), row);
    value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    MakeSmall(value, 9);
    row_layout->addWidget(name);
    row_layout->addWidget(bar, 1);
    row_layout->addWidget(value);
    doc_layout->addWidget(row);
    char_bars_[i] = bar;
    char_values_[i] = value;
  }
  layout->addWidget(doc_card);

  QVBoxLayout* history_layout = nullptr;
  auto* history_card =
      NewGroup(body, tr("Request History"), &history_layout, &history_hint_);
  history_card->setObjectName(QStringLiteral("statusHistoryGroup"));
  TrackGroup(history_card);
  auto* history_grid = new QGridLayout;
  history_grid->setContentsMargins(0, 0, 0, 0);
  history_grid->setSpacing(3);
  const QStringList history_names{tr("Start"), tr("Finish"), tr("Cancel"),
                                  tr("Stale"), tr("worker\\nfailed")};
  for (int i = 0; i < 5; ++i) {
    auto* cell = new QWidget(history_card);
    cell->setProperty("isCard", true);
    cell->setAttribute(Qt::WA_StyledBackground, true);
    auto* cell_layout = new QVBoxLayout(cell);
    cell_layout->setContentsMargins(2, 3, 2, 3);
    cell_layout->setSpacing(0);
    auto* value = new QLabel(QStringLiteral("0"), cell);
    value->setAlignment(Qt::AlignCenter);
    QFont value_font = value->font();
    value_font.setBold(true);
    value_font.setPixelSize(13);
    value->setFont(value_font);
    auto* name = new QLabel(history_names.at(i), cell);
    name->setAlignment(Qt::AlignCenter);
    MakeMuted(name);
    MakeSmall(name, 8);
    cell_layout->addWidget(value);
    cell_layout->addWidget(name);
    history_grid->addWidget(cell, 0, i);
    history_values_[i] = value;
  }
  history_layout->addLayout(history_grid);
  layout->addWidget(history_card);
  layout->addStretch();

  scroll->setWidget(body);
  panel_layout->addWidget(scroll);
  // 初始展开的抽屉在首次布局后才知道完整高度
  QTimer::singleShot(0, this, [this] { UpdateScrollContentHeight(); });
}

void RenderStatusPanel::UpdateScrollContentHeight() {
  if (scroll_body_ == nullptr || scroll_body_layout_ == nullptr) {
    return;
  }
  scroll_body_layout_->activate();
  scroll_body_->setMinimumHeight(scroll_body_layout_->sizeHint().height());
}

void RenderStatusPanel::TrackGroup(ExExpander* group) {
  connect(group, &ExExpander::expansionFinished, this,
          [this](bool) { UpdateScrollContentHeight(); });
}

void RenderStatusPanel::initSignals() {
  connect(refresh_button_, &QPushButton::clicked, this,
          &RenderStatusPanel::RefreshStatus);
  connect(reset_button_, &QPushButton::clicked, this, [this] {
    if (view_ == nullptr) {
      return;
    }
    view_->ResetRendererStats();
    RefreshStatus();
    emit statusMessage(tr("Render counters reset"));
  });
  ConnectViewSignals();
}

void RenderStatusPanel::ConnectViewSignals() {
  if (view_ == nullptr) {
    return;
  }
  // 文档重建（含主题/开关切换）都会更新计数，顺手刷新
  connect(view_.data(), &MarkdownView::outlineChanged, this,
          &RenderStatusPanel::RefreshStatus);
  connect(view_.data(), &MarkdownView::renderStatusChanged, this,
          &RenderStatusPanel::RefreshStatus);
  connect(view_.data(), &MarkdownView::renderProgress, this,
          [this](quint64 request_id, int completed, int total, int in_flight) {
            progress_request_ = request_id;
            progress_completed_ = completed;
            progress_total_ = total;
            progress_in_flight_ = in_flight;
            RefreshStatus();
          });
  connect(view_.data(), &MarkdownView::renderFinished, this,
          [this](quint64 request_id) {
            finished_request_ = request_id;
            progress_in_flight_ = 0;
            RefreshStatus();
          });
  connect(view_.data(), &MarkdownView::zoomChanged, this,
          &RenderStatusPanel::RefreshStatus);
  connect(view_.data(), &MarkdownView::viewportScrolled, this,
          &RenderStatusPanel::RefreshStatus);
}

void RenderStatusPanel::SetView(MarkdownView* view) {
  if (view == view_) {
    return;
  }
  if (view_ != nullptr) {
    disconnect(view_.data(), nullptr, this, nullptr);
  }
  view_ = view;
  ConnectViewSignals();
  RefreshStatus();
}

void RenderStatusPanel::RefreshStatus() {
  if (view_ == nullptr) {
    return;
  }
  const MarkdownRendererStatus status = view_->renderer_status();
  const MarkdownRenderContextOptions& options =
      view_->render_context().options();
  const MarkdownDiagnostics diagnostics = view_->diagnostics();
  const MarkdownServiceStatus service = view_->render_service_status();

  progress_hint_->setText(progress_request_ != 0
                              ? tr("Request #%1").arg(progress_request_)
                              : (finished_request_ != 0
                                     ? tr("Last #%1").arg(finished_request_)
                                     : tr("—")));
  QString ring_text = tr("—");
  if (progress_total_ > 0) {
    progress_ring_->setRange(0, progress_total_);
    progress_ring_->setValue(progress_completed_);
    progress_ring_->setFormat(QStringLiteral("%v/%m"));
    progress_ring_->setTextVisible(true);
    ring_text =
        QStringLiteral("%1/%2").arg(progress_completed_).arg(progress_total_);
    progress_percent_->setText(QStringLiteral("%1%").arg(
        qRound(100.0 * progress_completed_ / progress_total_)));
  } else if (view_->is_rendering()) {
    progress_ring_->setRange(0, 0);
    progress_ring_->setFormat(QString());
    progress_ring_->setTextVisible(false);
    progress_percent_->setText(tr("—"));
  } else {
    progress_ring_->setRange(0, 1);
    progress_ring_->setValue(0);
    progress_ring_->setFormat(tr("—"));
    progress_ring_->setTextVisible(true);
    progress_percent_->setText(tr("—"));
  }
  // 圆环内圈宽度固定，按任务数字宽收缩字号，避免 "1234/1234" 被省略
  QFont ring_font = progress_ring_->font();
  const int ring_width =
      progress_ring_->width() > 0 ? progress_ring_->width() : 72;
  ring_font.setPixelSize(
      FitRingFontPixelSize(ring_font, ring_text, qMax(16, ring_width - 24)));
  progress_ring_->setValueFont(ring_font);
  const int queued = service.highlight.queued + service.formula.queued +
                     service.mermaid.queued;
  progress_meta_->setText(
      view_->is_rendering()
          ? tr("%1 tasks in flight · %2 queued")
                .arg(progress_in_flight_)
                .arg(queued)
          : (progress_total_ > 0
                 ? tr("%1 tasks completed").arg(progress_completed_)
                 : tr("No tasks yet")));
  const MarkdownRenderHistory history = view_->render_history();
  const int alive =
      service.highlight.alive + service.formula.alive + service.mermaid.alive;
  const int failed = service.highlight.failed + service.formula.failed +
                     service.mermaid.failed;
  progress_chips_->setText(tr("workers alive %1 · failed %2 · canceled %3")
                               .arg(alive)
                               .arg(failed)
                               .arg(history.requests_cancelled));

  const MarkdownRenderTimings timings = view_->last_render_timings();
  const qint64 stage_values[kStageCount] = {
      timings.parse_ms,
      timings.collect_ms,
      timings.worker_ms,
      timings.assemble_ms,
      timings.layout_ms,
      qMax<qint64>(0, timings.total_ms - timings.parse_ms - timings.collect_ms -
                          timings.worker_ms - timings.assemble_ms -
                          timings.layout_ms)};
  qint64 stage_max = 1;
  for (qint64 value : stage_values) {
    stage_max = std::max(stage_max, value);
  }
  const int bar_max = static_cast<int>(
      std::min<qint64>(stage_max, std::numeric_limits<int>::max()));
  for (int i = 0; i < kStageCount; ++i) {
    QProgressBar* bar = stage_bars_[i];
    bar->setRange(0, bar_max);
    bar->setValue(static_cast<int>(
        std::min<qint64>(stage_values[i], std::numeric_limits<int>::max())));
    SetBarColor(bar, StageColor(i, this));
    if (i == 2) {
      bar->setToolTip(tr("worker %1 ms · slowest task %2 ms")
                          .arg(stage_values[i])
                          .arg(timings.worker_max_ms));
    } else {
      bar->setToolTip(QStringLiteral("%1 ms").arg(stage_values[i]));
    }
    stage_values_[i]->setText(QStringLiteral("%1 ms").arg(stage_values[i]));
  }
  timing_hint_->setText(tr("total %1 ms").arg(timings.total_ms));

  const MarkdownPoolStatus pool_statuses[3] = {
      service.highlight, service.formula, service.mermaid};
  for (int i = 0; i < 3; ++i) {
    const MarkdownPoolStatus& pool = pool_statuses[i];
    pools_[i].pips->SetStatus(pool.workers, pool.alive, pool.failed,
                              pool.processing);
    pools_[i].workers->setText(QStringLiteral("%1 workers").arg(pool.workers));
    pools_[i].meta->setText(tr("queued %1 · processed %2\\nJS %3")
                                .arg(pool.queued)
                                .arg(pool.processing)
                                .arg(BytesText(pool.js_memory_bytes)));
  }
  pools_hint_->setText(QStringLiteral("%1 + %2 + %3")
                           .arg(pool_statuses[0].workers)
                           .arg(pool_statuses[1].workers)
                           .arg(pool_statuses[2].workers));

  const MarkdownEngineStatus engine_statuses[3] = {
      status.highlighter, status.formula, status.mermaid};
  const MarkdownContextStatus contexts[3] = {
      status.highlight_context, status.formula_context, status.mermaid_context};
  for (int i = 0; i < 3; ++i) {
    const MarkdownEngineStatus& engine = engine_statuses[i];
    const MarkdownContextStatus& context = contexts[i];
    const QString engine_state =
        engine.permanently_failed
            ? tr("Load failed")
            : (engine.loaded ? tr("Loaded") : tr("Not loaded"));
    const StatusPill::Tone tone =
        engine.permanently_failed ? StatusPill::Tone::kBad
                                  : (engine.loaded ? StatusPill::Tone::kGood
                                                   : StatusPill::Tone::kMuted);
    engines_[i].pill->SetStatus(engine_state, tone);
    engines_[i].sub->setText(engine.loaded
                                 ? tr("first %1 ms").arg(engine.first_load_ms)
                                 : tr("first —"));
    engines_[i].bar->SetCounts(context.renders, context.memory_cache_hits,
                               context.disk_cache_hits);
    const quint64 calls =
        context.renders + context.memory_cache_hits + context.disk_cache_hits;
    engines_[i].detail->setText(tr("%1 · calls %2 · renders %3 · failures %4\n"
                                   "memory hits %5 · disk hits %6")
                                    .arg(engine_state)
                                    .arg(calls)
                                    .arg(context.renders)
                                    .arg(context.failures)
                                    .arg(context.memory_cache_hits)
                                    .arg(context.disk_cache_hits));
  }
  engines_hint_->setText(tr("calls = renders + hits"));

  const MarkdownCacheUsage cache = view_->render_context().cache_usage();
  const qint64 cache_bytes[4] = {cache.highlight_bytes, cache.formula_bytes,
                                 cache.mermaid_bytes, cache.pixmap_bytes};
  const int cache_entries[4] = {cache.highlight_entries, cache.formula_entries,
                                cache.mermaid_entries, cache.pixmap_entries};
  qint64 cache_max = 1;
  for (qint64 value : cache_bytes) {
    cache_max = std::max(cache_max, value);
  }
  const int cache_bar_max = static_cast<int>(
      std::min<qint64>(cache_max, std::numeric_limits<int>::max()));
  for (int i = 0; i < 4; ++i) {
    cache_bars_[i]->setRange(0, cache_bar_max);
    cache_bars_[i]->setValue(static_cast<int>(
        std::min<qint64>(cache_bytes[i], std::numeric_limits<int>::max())));
    SetBarColor(cache_bars_[i], StageColor(i, this));
    cache_values_[i]->setText(tr("%1 entries · %2")
                                  .arg(cache_entries[i])
                                  .arg(BytesText(cache_bytes[i])));
  }
  const qint64 image_limit = qMax<qint64>(1, cache.pixmap_limit_bytes);
  image_cap_bar_->setRange(
      0, static_cast<int>(
             std::min<qint64>(image_limit, std::numeric_limits<int>::max())));
  image_cap_bar_->setValue(static_cast<int>(
      std::min<qint64>(cache.pixmap_bytes, std::numeric_limits<int>::max())));
  SetBarColor(image_cap_bar_, AccentColor(this));
  image_cap_value_->setText(QStringLiteral("%1 / %2").arg(
      BytesText(cache.pixmap_bytes), BytesText(cache.pixmap_limit_bytes)));
  detail_cache_->setText(
      tr("%1 · folder: %2")
          .arg(OnOff(view_->persistent_render_cache_enabled()),
               options.cache_directory.isEmpty() ? tr("Default")
                                                 : options.cache_directory));
  detail_limits_->setText(
      tr("highlighting %1 · formulas %2 · Mermaid %3 · images %4 MB")
          .arg(options.highlight_cache_entries)
          .arg(options.formula_cache_entries)
          .arg(options.mermaid_cache_entries)
          .arg(options.image_cache_bytes / (1024 * 1024)));
  detail_memory_->setText(ProcessMemoryText());
  detail_diagnostics_->setText(
      QStringLiteral("trace %1 · dump %2")
          .arg(OnOff(diagnostics.trace), diagnostics.dump_directory.isEmpty()
                                             ? tr("Off")
                                             : diagnostics.dump_directory));
  cache_hint_->setText(QStringLiteral("memory cache"));

  const MarkdownDocumentStats document = view_->document_stats();
  const int document_values[4] = {document.headings, document.code_blocks,
                                  document.formulas, document.mermaid_blocks};
  for (int i = 0; i < 4; ++i) {
    doc_values_[i]->setText(QString::number(document_values[i]));
  }
  doc_hint_->setText(tr("Current Document"));
  const qint64 char_values[2] = {document.source_chars, document.html_chars};
  const qint64 char_max = qMax<qint64>(1, char_values[1]);
  for (int i = 0; i < 2; ++i) {
    char_bars_[i]->setRange(0, static_cast<int>(std::min<qint64>(
                                   char_max, std::numeric_limits<int>::max())));
    char_bars_[i]->setValue(static_cast<int>(
        std::min<qint64>(char_values[i], std::numeric_limits<int>::max())));
    SetBarColor(char_bars_[i], AccentColor(this));
    char_values_[i]->setText(tr("%1 chars").arg(char_values[i]));
  }

  const quint64 history_values[5] = {
      history.requests_started, history.requests_finished,
      history.requests_cancelled, history.stale_results,
      history.failed_worker_results};
  for (int i = 0; i < 5; ++i) {
    history_values_[i]->setText(QString::number(history_values[i]));
  }
  history_hint_->setText(tr("Total for this view"));
}

// HitBar 在本文件里带了 Q_OBJECT，AUTOMOC 要求这里包含 moc 产物
#include "render_status_panel.moc"
