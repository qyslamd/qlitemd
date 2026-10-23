#include "quick_open.h"

#include <algorithm>

#include <QApplication>
#include <QEvent>
#include <QFontMetrics>
#include <QFrame>
#include <QGraphicsDropShadowEffect>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QIcon>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPainter>
#include <QScreen>
#include <QStyle>
#include <QStyledItemDelegate>
#include <QVBoxLayout>

namespace {

constexpr int kItemHeight = 54;
constexpr int kPanelRadius = 12;

enum ItemRole {
  kSubtitleRole = Qt::UserRole,
  kKindRole,
  kKindTextRole,
};

QString KindText(QuickOpenKind kind) {
  switch (kind) {
    case QuickOpenKind::kHeading:
      return QStringLiteral("标题");
    case QuickOpenKind::kFile:
      return QStringLiteral("文件");
    case QuickOpenKind::kRecent:
      return QStringLiteral("最近");
    case QuickOpenKind::kCommand:
      return QStringLiteral("命令");
  }
  return QString();
}

QString KindLetter(QuickOpenKind kind) {
  switch (kind) {
    case QuickOpenKind::kHeading:
      return QStringLiteral("H");
    case QuickOpenKind::kFile:
      return QStringLiteral("F");
    case QuickOpenKind::kRecent:
      return QStringLiteral("R");
    case QuickOpenKind::kCommand:
      return QStringLiteral(">");
  }
  return QString();
}

QFont ScaledFont(const QFont& base, qreal delta, bool bold) {
  QFont font = base;
  if (font.pointSizeF() > 0.0) {
    font.setPointSizeF(qMax(6.0, font.pointSizeF() + delta));
  } else {
    font.setPixelSize(qMax(8, font.pixelSize() + qRound(delta)));
  }
  if (bold) {
    font.setWeight(QFont::DemiBold);
  }
  return font;
}

// 面板自己画圆角与描边：示例不加载 QSS，Fluent 样式只负责基础控件
class QuickOpenPanel final : public QFrame {
 public:
  explicit QuickOpenPanel(QWidget* parent = nullptr) : QFrame(parent) {}

 protected:
  void paintEvent(QPaintEvent* event) override {
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    QColor background = palette().color(QPalette::Base);
    QColor border = palette().color(QPalette::Mid);
    border.setAlpha(110);
    painter.setPen(QPen(border, 1.0));
    painter.setBrush(background);
    painter.drawRoundedRect(rect().adjusted(0, 0, -1, -1), kPanelRadius,
                            kPanelRadius);
  }
};

class QuickOpenDelegate final : public QStyledItemDelegate {
 public:
  explicit QuickOpenDelegate(QObject* parent = nullptr)
      : QStyledItemDelegate(parent) {}

  QSize sizeHint(const QStyleOptionViewItem& option,
                 const QModelIndex& index) const override {
    Q_UNUSED(option);
    Q_UNUSED(index);
    return QSize(0, kItemHeight);
  }

  void paint(QPainter* painter, const QStyleOptionViewItem& option,
             const QModelIndex& index) const override {
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);

    const bool selected = option.state & QStyle::State_Selected;
    const QPalette& palette = option.palette;
    const QColor accent = palette.color(QPalette::Highlight);
    const QColor title_color = selected
                                   ? palette.color(QPalette::HighlightedText)
                                   : palette.color(QPalette::Text);
    QColor subtitle_color =
        selected ? title_color : palette.color(QPalette::PlaceholderText);
    if (selected) {
      subtitle_color.setAlpha(205);
    }

    const QRect row = option.rect.adjusted(4, 2, -4, -2);
    if (selected) {
      painter->setPen(Qt::NoPen);
      painter->setBrush(accent);
      painter->drawRoundedRect(row, 8, 8);
    }

    const QRect icon_rect(row.left() + 10, row.center().y() - 11, 22, 22);
    QColor icon_background = accent;
    icon_background.setAlpha(selected ? 48 : 28);
    painter->setPen(Qt::NoPen);
    painter->setBrush(icon_background);
    painter->drawRoundedRect(icon_rect, 6, 6);
    painter->setPen(selected ? title_color : accent);
    painter->setFont(ScaledFont(option.font, -1.0, true));
    painter->drawText(
        icon_rect, Qt::AlignCenter,
        KindLetter(static_cast<QuickOpenKind>(index.data(kKindRole).toInt())));

    const QString kind_text = index.data(kKindTextRole).toString();
    QFont kind_font = ScaledFont(option.font, -2.0, false);
    const QFontMetrics kind_metrics(kind_font);
    const int kind_width = kind_metrics.horizontalAdvance(kind_text);
    painter->setFont(kind_font);
    QColor kind_color = subtitle_color;
    if (!selected) {
      kind_color.setAlpha(180);
    }
    painter->setPen(kind_color);
    painter->drawText(QRect(row.right() - 12 - kind_width, row.top(),
                            kind_width, row.height()),
                      Qt::AlignVCenter | Qt::AlignRight, kind_text);

    const int text_left = icon_rect.right() + 10;
    const int text_right = row.right() - 12 - kind_width - 12;
    const int text_width = qMax(20, text_right - text_left);
    QFont title_font = ScaledFont(option.font, 0.5, true);
    painter->setFont(title_font);
    painter->setPen(title_color);
    const QFontMetrics title_metrics(title_font);
    painter->drawText(
        QRect(text_left, row.top() + 7, text_width, 21),
        Qt::AlignLeft | Qt::AlignVCenter,
        title_metrics.elidedText(index.data(Qt::DisplayRole).toString(),
                                 Qt::ElideRight, text_width));

    QFont subtitle_font = ScaledFont(option.font, -1.5, false);
    painter->setFont(subtitle_font);
    painter->setPen(subtitle_color);
    const QFontMetrics subtitle_metrics(subtitle_font);
    painter->drawText(
        QRect(text_left, row.top() + 27, text_width, 18),
        Qt::AlignLeft | Qt::AlignVCenter,
        subtitle_metrics.elidedText(index.data(kSubtitleRole).toString(),
                                    Qt::ElideMiddle, text_width));
    painter->restore();
  }
};

}  // namespace

int QuickOpenScore(const QString& text, const QString& query) {
  if (query.isEmpty()) {
    return 1;
  }
  if (text.isEmpty()) {
    return 0;
  }
  int score = 0;
  int query_index = 0;
  int last_match = -2;
  for (int i = 0; i < text.size() && query_index < query.size(); ++i) {
    if (text.at(i).toCaseFolded() != query.at(query_index).toCaseFolded()) {
      continue;
    }
    int bonus = 1;
    if (i == last_match + 1) {
      bonus += 4;
    }
    const bool word_start = i == 0 || text.at(i - 1).isSpace() ||
                            text.at(i - 1) == QLatin1Char('/') ||
                            text.at(i - 1) == QLatin1Char('-') ||
                            text.at(i - 1) == QLatin1Char('_');
    if (word_start) {
      bonus += 6;
    }
    score += bonus;
    last_match = i;
    ++query_index;
  }
  if (query_index < query.size()) {
    return 0;
  }
  if (text.contains(query, Qt::CaseInsensitive)) {
    score += 12;
  }
  score += qMax(0, 24 - text.size() / 4);
  return score;
}

QVector<QuickOpenItem> FilterQuickOpenItems(const QVector<QuickOpenItem>& items,
                                            const QString& query,
                                            int max_results) {
  if (max_results <= 0) {
    return {};
  }
  const QString needle = query.trimmed();
  if (needle.isEmpty()) {
    return items.mid(0, max_results);
  }

  struct Ranked {
    int score = 0;
    int index = 0;
  };
  QVector<Ranked> ranked;
  ranked.reserve(items.size());
  for (int i = 0; i < items.size(); ++i) {
    const QuickOpenItem& item = items.at(i);
    const int title_score = QuickOpenScore(item.title, needle);
    const int subtitle_score = QuickOpenScore(item.subtitle, needle);
    if (title_score == 0 && subtitle_score == 0) {
      continue;
    }
    int score = title_score * 3 + subtitle_score;
    if (item.title.compare(needle, Qt::CaseInsensitive) == 0) {
      score += 1000;
    } else if (item.title.startsWith(needle, Qt::CaseInsensitive)) {
      score += 250;
    }
    ranked.append({score, i});
  }
  std::stable_sort(ranked.begin(), ranked.end(),
                   [](const Ranked& left, const Ranked& right) {
                     return left.score > right.score;
                   });

  QVector<QuickOpenItem> result;
  result.reserve(qMin(max_results, ranked.size()));
  for (int i = 0; i < ranked.size() && result.size() < max_results; ++i) {
    result.append(items.at(ranked.at(i).index));
  }
  return result;
}

QuickOpenPopup::QuickOpenPopup(QWidget* parent) : QWidget(parent) {
  setObjectName(QStringLiteral("quickOpenPopup"));
  setWindowFlags(Qt::Popup | Qt::FramelessWindowHint);
  setAttribute(Qt::WA_TranslucentBackground);
  setFocusPolicy(Qt::StrongFocus);

  auto* outer = new QVBoxLayout(this);
  outer->setContentsMargins(18, 18, 18, 18);
  outer->setSpacing(0);

  auto* panel = new QuickOpenPanel(this);
  panel->setObjectName(QStringLiteral("quickOpenPanel"));
  auto* shadow = new QGraphicsDropShadowEffect(panel);
  shadow->setBlurRadius(32);
  shadow->setOffset(0, 8);
  shadow->setColor(QColor(0, 0, 0, 90));
  panel->setGraphicsEffect(shadow);

  auto* layout = new QVBoxLayout(panel);
  layout->setContentsMargins(12, 10, 12, 10);
  layout->setSpacing(8);

  auto* search_row = new QHBoxLayout;
  search_row->setContentsMargins(4, 0, 4, 0);
  search_row->setSpacing(8);
  auto* icon = new QLabel(panel);
  icon->setObjectName(QStringLiteral("quickOpenIcon"));
  icon->setPixmap(
      QIcon(QStringLiteral(":/resources/svg/search.svg")).pixmap(16, 16));
  icon->setFixedSize(16, 16);
  search_row->addWidget(icon);
  query_edit_ = new QLineEdit(panel);
  query_edit_->setObjectName(QStringLiteral("quickOpenQuery"));
  query_edit_->setPlaceholderText(QStringLiteral("跳转到文件、标题或命令…"));
  query_edit_->setFrame(false);
  query_edit_->setClearButtonEnabled(false);
  QFont query_font = font();
  if (query_font.pointSizeF() > 0.0) {
    query_font.setPointSizeF(query_font.pointSizeF() + 1.0);
  }
  query_edit_->setFont(query_font);
  query_edit_->installEventFilter(this);
  connect(query_edit_, &QLineEdit::textChanged, this,
          &QuickOpenPopup::ApplyFilter);
  search_row->addWidget(query_edit_, 1);
  auto* escape_hint = new QLabel(QStringLiteral("Esc"), panel);
  escape_hint->setObjectName(QStringLiteral("quickOpenEscapeHint"));
  escape_hint->setForegroundRole(QPalette::PlaceholderText);
  search_row->addWidget(escape_hint);
  layout->addLayout(search_row);

  auto* divider = new QFrame(panel);
  divider->setObjectName(QStringLiteral("quickOpenDivider"));
  divider->setFrameShape(QFrame::HLine);
  divider->setFrameShadow(QFrame::Plain);
  layout->addWidget(divider);

  list_ = new QListWidget(panel);
  list_->setObjectName(QStringLiteral("quickOpenList"));
  list_->setFrameShape(QFrame::NoFrame);
  list_->setFocusPolicy(Qt::NoFocus);
  list_->setSelectionMode(QAbstractItemView::SingleSelection);
  list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  list_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
  list_->setUniformItemSizes(true);
  list_->setItemDelegate(new QuickOpenDelegate(list_));
  list_->setAttribute(Qt::WA_TranslucentBackground);
  list_->viewport()->setAutoFillBackground(false);
  // 鼠标点击也要能执行；Enter 走 eventFilter，双击走 itemActivated
  auto activate_item = [this](QListWidgetItem* item) {
    if (item != nullptr) {
      list_->setCurrentItem(item);
    }
    ActivateCurrent();
  };
  connect(list_, &QListWidget::itemClicked, this, activate_item);
  connect(list_, &QListWidget::itemActivated, this, activate_item);
  layout->addWidget(list_, 1);

  empty_label_ = new QLabel(QStringLiteral("没有匹配项"), panel);
  empty_label_->setObjectName(QStringLiteral("quickOpenEmpty"));
  empty_label_->setAlignment(Qt::AlignCenter);
  empty_label_->setForegroundRole(QPalette::PlaceholderText);
  empty_label_->hide();
  layout->addWidget(empty_label_, 1);

  auto* footer = new QHBoxLayout;
  footer->setContentsMargins(4, 0, 4, 2);
  count_label_ = new QLabel(panel);
  count_label_->setObjectName(QStringLiteral("quickOpenCount"));
  count_label_->setForegroundRole(QPalette::PlaceholderText);
  auto* hint = new QLabel(QStringLiteral("↑↓ 选择 · Enter 打开"), panel);
  hint->setObjectName(QStringLiteral("quickOpenHint"));
  hint->setForegroundRole(QPalette::PlaceholderText);
  QFont footer_font = font();
  if (footer_font.pointSizeF() > 0.0) {
    footer_font.setPointSizeF(qMax(8.0, footer_font.pointSizeF() - 1.5));
  }
  count_label_->setFont(footer_font);
  hint->setFont(footer_font);
  footer->addWidget(count_label_);
  footer->addStretch(1);
  footer->addWidget(hint);
  layout->addLayout(footer);

  outer->addWidget(panel);
}

void QuickOpenPopup::Open(const QVector<QuickOpenItem>& items,
                          const QString& query) {
  source_items_ = items;
  query_edit_->clear();
  if (!query.isEmpty()) {
    query_edit_->setText(query);
  }
  ApplyFilter();
  PositionNearParent();
  show();
  raise();
  activateWindow();
  query_edit_->setFocus();
}

bool QuickOpenPopup::eventFilter(QObject* watched, QEvent* event) {
  if (watched == query_edit_ && event->type() == QEvent::KeyPress) {
    auto* key = static_cast<QKeyEvent*>(event);
    switch (key->key()) {
      case Qt::Key_Down:
        MoveSelection(1);
        return true;
      case Qt::Key_Up:
        MoveSelection(-1);
        return true;
      case Qt::Key_PageDown:
        MoveSelection(6);
        return true;
      case Qt::Key_PageUp:
        MoveSelection(-6);
        return true;
      case Qt::Key_Return:
      case Qt::Key_Enter:
        ActivateCurrent();
        return true;
      case Qt::Key_Escape:
        hide();
        return true;
      default:
        break;
    }
  }
  return QWidget::eventFilter(watched, event);
}

void QuickOpenPopup::hideEvent(QHideEvent* event) {
  query_edit_->clear();
  QWidget::hideEvent(event);
}

void QuickOpenPopup::ApplyFilter() {
  shown_items_ = FilterQuickOpenItems(source_items_, query_edit_->text());
  list_->clear();
  for (const QuickOpenItem& item : shown_items_) {
    auto* row = new QListWidgetItem(item.title, list_);
    row->setData(kSubtitleRole, item.subtitle);
    row->setData(kKindRole, static_cast<int>(item.kind));
    row->setData(kKindTextRole, KindText(item.kind));
  }
  const bool empty = shown_items_.isEmpty();
  list_->setVisible(!empty);
  empty_label_->setVisible(empty);
  count_label_->setText(QStringLiteral("%1 项").arg(shown_items_.size()));
  if (!empty) {
    list_->setCurrentRow(0);
  }
}

void QuickOpenPopup::ActivateCurrent() {
  const int row = list_->currentRow();
  if (row < 0 || row >= shown_items_.size()) {
    return;
  }
  const QuickOpenItem item = shown_items_.at(row);
  hide();
  emit itemActivated(item);
}

void QuickOpenPopup::MoveSelection(int delta) {
  if (shown_items_.isEmpty()) {
    return;
  }
  const int current = qMax(0, list_->currentRow());
  const int next = qBound(0, current + delta, shown_items_.size() - 1);
  list_->setCurrentRow(next);
  list_->scrollToItem(list_->item(next));
}

void QuickOpenPopup::PositionNearParent() {
  QWidget* parent = parentWidget();
  if (parent == nullptr) {
    return;
  }
  const int width = qBound(520, parent->width() - 120, 720);
  const int height = qBound(320, parent->height() - 200, 480);
  QPoint top_left =
      parent->mapToGlobal(QPoint((parent->width() - width) / 2, 78));

  QScreen* screen = QGuiApplication::screenAt(top_left);
  if (screen == nullptr) {
    screen = QGuiApplication::primaryScreen();
  }
  if (screen != nullptr) {
    const QRect available = screen->availableGeometry();
    top_left.setX(qBound(available.left() + 12, top_left.x(),
                         available.right() - width - 12));
    top_left.setY(qBound(available.top() + 12, top_left.y(),
                         available.bottom() - height - 12));
  }
  setGeometry(QRect(top_left, QSize(width, height)));
}
