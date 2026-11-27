#include "command_palette.h"

#include <QEvent>
#include <QFrame>
#include <QKeyEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QVBoxLayout>

namespace {

constexpr int kCardWidth = 560;
constexpr int kCardMargin = 14;

QString ItemText(const PaletteItem& item) {
  return item.title + QStringLiteral("    ") + item.detail;
}

}  // namespace

CommandPalette::CommandPalette(QWidget* parent) : QWidget(parent) {
  setObjectName(QStringLiteral("paletteOverlay"));
  setAttribute(Qt::WA_StyledBackground, false);
  setFocusPolicy(Qt::StrongFocus);
  hide();

  card_ = new QFrame(this);
  card_->setObjectName(QStringLiteral("paletteCard"));
  card_->setFrameShape(QFrame::StyledPanel);
  card_->setAutoFillBackground(true);
  card_->setBackgroundRole(QPalette::Base);
  card_->setAttribute(Qt::WA_StyledBackground, true);
  auto* layout = new QVBoxLayout(card_);
  layout->setContentsMargins(0, 0, 0, 8);
  layout->setSpacing(0);

  input_ = new QLineEdit(card_);
  input_->setObjectName(QStringLiteral("paletteInput"));
  input_->setPlaceholderText(tr("Search documents, sections and commands"));
  input_->installEventFilter(this);
  layout->addWidget(input_);

  list_ = new QListWidget(card_);
  list_->setObjectName(QStringLiteral("paletteList"));
  list_->setFrameShape(QFrame::NoFrame);
  list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  list_->setUniformItemSizes(false);
  list_->setSpacing(0);
  list_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
  layout->addWidget(list_, 1);

  connect(input_, &QLineEdit::textChanged, this,
          [this](const QString& text) { Rebuild(text); });
  connect(input_, &QLineEdit::returnPressed, this,
          &CommandPalette::ActivateCurrent);
  connect(list_, &QListWidget::itemActivated, this,
          [this](QListWidgetItem*) { ActivateCurrent(); });
  connect(list_, &QListWidget::itemClicked, this,
          [this](QListWidgetItem*) { ActivateCurrent(); });

  SetDark(true);
}

void CommandPalette::SetItems(QVector<PaletteItem> items) {
  items_ = std::move(items);
  if (isVisible()) {
    Rebuild(input_->text());
  }
}

void CommandPalette::SetDark(bool dark) {
  const QString surface =
      dark ? QStringLiteral("#232428") : QStringLiteral("#efeee9");
  const QString border =
      dark ? QStringLiteral("#2b2c31") : QStringLiteral("#dddcd5");
  const QString text =
      dark ? QStringLiteral("#e7e6e2") : QStringLiteral("#1f1f1d");
  const QString selection = dark ? QStringLiteral("rgba(217,119,87,46)")
                                 : QStringLiteral("rgba(184,92,62,40)");
  setStyleSheet(
      QStringLiteral(
          "QFrame#paletteCard { background:%1; border:1px solid %2; "
          "border-radius:10px; }"
          "QLineEdit#paletteInput { background:transparent; border:none; "
          "border-bottom:1px solid %2; padding:10px 12px; font-size:14px; "
          "color:%3; }"
          "QListWidget#paletteList { background:transparent; border:none; "
          "color:%3; }"
          "QListWidget#paletteList::item { height:30px; padding-left:10px; }"
          "QListWidget#paletteList::item:selected { background:%4; }")
          .arg(surface, border, text, selection));
}

void CommandPalette::Open() {
  if (parentWidget() != nullptr) {
    setGeometry(parentWidget()->rect());
  }
  Rebuild(QString());
  input_->clear();
  show();
  raise();
  input_->setFocus(Qt::ShortcutFocusReason);
}

void CommandPalette::Close() {
  if (!isVisible()) {
    return;
  }
  hide();
  emit closed();
}

bool CommandPalette::is_open() const { return isVisible(); }

void CommandPalette::Rebuild(const QString& filter) {
  const QString needle = filter.trimmed();
  filtered_.clear();
  list_->clear();
  for (int i = 0; i < items_.size(); ++i) {
    const PaletteItem& item = items_[i];
    const bool matches = needle.isEmpty() ||
                         item.title.contains(needle, Qt::CaseInsensitive) ||
                         item.detail.contains(needle, Qt::CaseInsensitive) ||
                         item.category.contains(needle, Qt::CaseInsensitive);
    if (!matches) {
      continue;
    }
    filtered_.push_back(i);
    auto* row = new QListWidgetItem(
        QStringLiteral("%1    %2").arg(item.category, ItemText(item)), list_);
    row->setSizeHint(QSize(0, 30));
    row->setToolTip(item.detail);
    if (filtered_.size() >= 60) {
      break;
    }
  }
  if (list_->count() > 0) {
    list_->setCurrentRow(0);
  }
}

void CommandPalette::ActivateCurrent() {
  const int row = list_->currentRow();
  if (row < 0 || row >= filtered_.size()) {
    return;
  }
  const int index = filtered_[row];
  if (index < 0 || index >= items_.size()) {
    return;
  }
  const std::function<void()> action = items_[index].action;
  Close();
  if (action) {
    action();
  }
}

bool CommandPalette::eventFilter(QObject* watched, QEvent* event) {
  if (watched == input_ && event->type() == QEvent::KeyPress) {
    auto* key = static_cast<QKeyEvent*>(event);
    if (key->key() == Qt::Key_Escape) {
      Close();
      return true;
    }
    if (key->key() == Qt::Key_Down || key->key() == Qt::Key_Up) {
      const int delta = key->key() == Qt::Key_Down ? 1 : -1;
      const int row =
          qBound(0, list_->currentRow() + delta, list_->count() - 1);
      list_->setCurrentRow(row);
      return true;
    }
  }
  return QWidget::eventFilter(watched, event);
}

void CommandPalette::paintEvent(QPaintEvent* event) {
  (void)event;
  QPainter painter(this);
  painter.fillRect(rect(), QColor(0, 0, 0, 120));
}

void CommandPalette::resizeEvent(QResizeEvent* event) {
  QWidget::resizeEvent(event);
  const int height = qMin(480, qMax(220, this->height() - 160));
  const int width = qMin(kCardWidth, this->width() - 2 * kCardMargin);
  card_->setGeometry((this->width() - width) / 2, 64, width, height);
}

void CommandPalette::mousePressEvent(QMouseEvent* event) {
  if (!card_->geometry().contains(event->pos())) {
    Close();
  }
  QWidget::mousePressEvent(event);
}
