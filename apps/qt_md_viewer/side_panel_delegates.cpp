#include "side_panel_delegates.h"

#include <algorithm>
#include <utility>

#include <QAbstractItemModel>
#include <QEvent>
#include <QFontMetrics>
#include <QIcon>
#include <QLocale>
#include <QMouseEvent>
#include <QPainter>
#include <QPalette>
#include <QStyle>
#include <QStyleOptionViewItem>

#include "side_panel_roles.h"
#include "text_search.h"
#include "theme_icons.h"

namespace {

constexpr int kTreeRowHeight = 28;
constexpr int kBookmarkRowHeight = 46;
constexpr int kStarSlotWidth = 22;
constexpr int kIconSize = 16;

QFont SmallFont(const QFont& base) {
  QFont small = base;
  if (small.pixelSize() > 0) {
    small.setPixelSize(std::max(10, small.pixelSize() - 2));
  } else if (small.pointSizeF() > 0.0) {
    small.setPointSizeF(std::max(7.0, small.pointSizeF() - 1.0));
  }
  return small;
}

QColor DimText(const QPalette& palette) {
  QColor color = palette.color(QPalette::Text);
  color.setAlpha(150);
  return color;
}

void PaintRowBackground(QPainter* painter, const QStyleOptionViewItem& opt,
                        bool selected, bool hovered) {
  if (!selected && !hovered) {
    return;
  }
  QColor fill = opt.palette.color(QPalette::Highlight);
  fill.setAlpha(selected ? 34 : 16);
  painter->setPen(Qt::NoPen);
  painter->setBrush(fill);
  painter->drawRoundedRect(opt.rect.adjusted(2, 1, -2, -1), 6, 6);
}

}  // namespace

DocumentTreeDelegate::DocumentTreeDelegate(QWidget* parent,
                                           ToggleCallback toggle)
    : QStyledItemDelegate(parent), toggle_(std::move(toggle)) {}

void DocumentTreeDelegate::paint(QPainter* painter,
                                 const QStyleOptionViewItem& option,
                                 const QModelIndex& index) const {
  QStyleOptionViewItem opt = option;
  initStyleOption(&opt, index);
  const bool selected = (opt.state & QStyle::State_Selected) != 0;
  const bool hovered = (opt.state & QStyle::State_MouseOver) != 0;
  const bool is_directory = index.data(kIsDirectoryRole).toBool();
  const bool bookmarked = index.data(kBookmarkedRole).toBool();

  painter->save();
  painter->setRenderHint(QPainter::Antialiasing, true);
  PaintRowBackground(painter, opt, selected, hovered);

  const QRect icon_rect(opt.rect.left() + 4,
                        opt.rect.top() + (opt.rect.height() - kIconSize) / 2,
                        kIconSize, kIconSize);
  const QIcon icon = ThemeIcon(is_directory ? "folder" : "file", dark_);
  icon.paint(painter, icon_rect, Qt::AlignCenter);

  const bool show_star = !is_directory && (bookmarked || hovered || selected);
  const QRect star_rect(opt.rect.right() - kStarSlotWidth,
                        opt.rect.top() + (opt.rect.height() - kIconSize) / 2,
                        kStarSlotWidth, kIconSize);

  const QFontMetrics metrics(opt.font);
  const QString right_text =
      is_directory
          ? QString::number(index.data(kCountRole).toInt())
          : QLocale().formattedDataSize(index.data(kSizeRole).toLongLong(), 1,
                                        QLocale::DataSizeTraditionalFormat);
  const int right_edge =
      show_star ? star_rect.left() - 4 : opt.rect.right() - 6;
  const int right_width = metrics.horizontalAdvance(right_text);
  const QRect right_rect(right_edge - right_width, opt.rect.top(), right_width,
                         opt.rect.height());
  const QRect name_rect(icon_rect.right() + 6, opt.rect.top(),
                        std::max(0, right_rect.left() - icon_rect.right() - 12),
                        opt.rect.height());

  if (show_star) {
    // star-filled 是语义图标（金色），不随主题换色；outline 走主题套
    const QIcon star =
        bookmarked ? QIcon(QStringLiteral(":/resources/svg/star-filled.svg"))
                   : ThemeIcon(QStringLiteral("star-outline"), dark_);
    star.paint(painter, star_rect, Qt::AlignCenter);
  }

  QFont name_font = opt.font;
  name_font.setBold(selected);
  const QFontMetrics name_metrics(name_font);
  const QString name =
      name_metrics.elidedText(index.data(Qt::DisplayRole).toString(),
                              Qt::ElideRight, name_rect.width());
  painter->setFont(name_font);
  painter->setPen(
      opt.palette.color(selected ? QPalette::Highlight : QPalette::Text));
  painter->drawText(name_rect, Qt::AlignVCenter | Qt::AlignLeft, name);

  painter->setFont(opt.font);
  painter->setPen(DimText(opt.palette));
  painter->drawText(right_rect, Qt::AlignVCenter | Qt::AlignRight, right_text);
  painter->restore();
}

QSize DocumentTreeDelegate::sizeHint(const QStyleOptionViewItem& option,
                                     const QModelIndex& index) const {
  Q_UNUSED(index);
  return QSize(
      0, std::max(kTreeRowHeight, QFontMetrics(option.font).height() + 10));
}

bool DocumentTreeDelegate::editorEvent(QEvent* event, QAbstractItemModel* model,
                                       const QStyleOptionViewItem& option,
                                       const QModelIndex& index) {
  Q_UNUSED(model);
  if (event->type() != QEvent::MouseButtonRelease ||
      index.data(kIsDirectoryRole).toBool()) {
    return false;
  }
  auto* mouse_event = static_cast<QMouseEvent*>(event);
  if (mouse_event->button() != Qt::LeftButton) {
    return false;
  }
  const QRect star_rect(
      option.rect.right() - kStarSlotWidth,
      option.rect.top() + (option.rect.height() - kIconSize) / 2,
      kStarSlotWidth, kIconSize);
  if (!star_rect.contains(mouse_event->pos())) {
    return false;
  }
  const QString path = index.data(kPathRole).toString();
  if (toggle_ && !path.isEmpty()) {
    toggle_(path);
  }
  return true;
}

void BookmarkDelegate::paint(QPainter* painter,
                             const QStyleOptionViewItem& option,
                             const QModelIndex& index) const {
  QStyleOptionViewItem opt = option;
  initStyleOption(&opt, index);
  const bool selected = (opt.state & QStyle::State_Selected) != 0;
  const bool hovered = (opt.state & QStyle::State_MouseOver) != 0;
  const bool missing = index.data(kMissingRole).toBool();

  painter->save();
  painter->setRenderHint(QPainter::Antialiasing, true);
  PaintRowBackground(painter, opt, selected, hovered);

  const QRect star_rect(opt.rect.left() + 4,
                        opt.rect.top() + (opt.rect.height() - kIconSize) / 2,
                        kIconSize, kIconSize);
  // 实心星是语义图标（金色）；缺失项的空星走主题套
  const QIcon star =
      missing ? ThemeIcon(QStringLiteral("star-outline"), dark_)
              : QIcon(QStringLiteral(":/resources/svg/star-filled.svg"));
  star.paint(painter, star_rect, Qt::AlignCenter);

  const QRect text_rect = opt.rect.adjusted(28, 4, -8, -4);
  const int name_height = text_rect.height() / 2;
  const QRect name_rect(text_rect.left(), text_rect.top(), text_rect.width(),
                        name_height);
  const QRect path_rect(text_rect.left(), text_rect.top() + name_height,
                        text_rect.width(), text_rect.height() - name_height);

  const QColor text_color =
      missing ? opt.palette.color(QPalette::Disabled, QPalette::Text)
              : opt.palette.color(QPalette::Text);
  const QColor path_color = missing ? text_color : DimText(opt.palette);

  QFont name_font = opt.font;
  name_font.setBold(true);
  painter->setFont(name_font);
  painter->setPen(text_color);
  const QFontMetrics name_metrics(name_font);
  painter->drawText(
      name_rect, Qt::AlignVCenter | Qt::AlignLeft,
      name_metrics.elidedText(index.data(Qt::DisplayRole).toString(),
                              Qt::ElideRight, name_rect.width()));

  painter->setFont(SmallFont(opt.font));
  painter->setPen(path_color);
  const QFontMetrics path_metrics(SmallFont(opt.font));
  painter->drawText(
      path_rect, Qt::AlignVCenter | Qt::AlignLeft,
      path_metrics.elidedText(index.data(kBookmarkDisplayPathRole).toString(),
                              Qt::ElideRight, path_rect.width()));
  painter->restore();
}

QSize BookmarkDelegate::sizeHint(const QStyleOptionViewItem& option,
                                 const QModelIndex& index) const {
  Q_UNUSED(index);
  const int content_height = QFontMetrics(option.font).height() +
                             QFontMetrics(SmallFont(option.font)).height() + 12;
  return QSize(0, std::max(kBookmarkRowHeight, content_height));
}

void SearchResultDelegate::paint(QPainter* painter,
                                 const QStyleOptionViewItem& option,
                                 const QModelIndex& index) const {
  if (!index.parent().isValid()) {
    QStyledItemDelegate::paint(painter, option, index);
    return;
  }
  QStyleOptionViewItem opt = option;
  initStyleOption(&opt, index);
  painter->save();
  painter->setRenderHint(QPainter::Antialiasing, true);
  const bool selected = (opt.state & QStyle::State_Selected) != 0;
  const bool hovered = (opt.state & QStyle::State_MouseOver) != 0;
  PaintRowBackground(painter, opt, selected, hovered);

  const SearchHit hit = index.data(kSearchHitRole).value<SearchHit>();
  const int query_length = index.data(kQueryLengthRole).toInt();
  const QFontMetrics metrics(opt.font);
  const QRect line_rect(opt.rect.left() + 8, opt.rect.top(), 36,
                        opt.rect.height());
  painter->setPen(opt.palette.color(QPalette::Disabled, QPalette::Text));
  painter->drawText(line_rect, Qt::AlignVCenter | Qt::AlignLeft,
                    QStringLiteral("L%1").arg(hit.line));

  const QRect text_rect = opt.rect.adjusted(48, 0, -8, 0);
  const QColor text_color = opt.palette.color(QPalette::Text);
  // 侧栏结果用稿子的黄色 mark；正文里的选区仍由 FindText 决定
  const bool dark = opt.palette.color(QPalette::Window).lightness() < 128;
  const QColor mark_color =
      dark ? QColor(255, 214, 102, 90) : QColor(255, 196, 0, 72);
  const QString snippet =
      metrics.elidedText(hit.snippet, Qt::ElideRight, text_rect.width());
  const int visible_length = snippet.size();
  const int match_start = hit.match_offset;
  const int match_end = std::min(visible_length, match_start + query_length);
  if (match_start >= 0 && match_start < visible_length &&
      match_end > match_start) {
    const QString before = snippet.left(match_start);
    const QString match = snippet.mid(match_start, match_end - match_start);
    const QString after = snippet.mid(match_end);
    const int before_width = metrics.horizontalAdvance(before);
    const int match_width = metrics.horizontalAdvance(match);
    painter->fillRect(
        QRect(text_rect.left() + before_width, text_rect.top() + 2, match_width,
              text_rect.height() - 4),
        mark_color);
    painter->setPen(text_color);
    painter->drawText(text_rect, Qt::AlignVCenter | Qt::AlignLeft, before);
    painter->drawText(text_rect.adjusted(before_width, 0, 0, 0),
                      Qt::AlignVCenter | Qt::AlignLeft, match);
    painter->drawText(text_rect.adjusted(before_width + match_width, 0, 0, 0),
                      Qt::AlignVCenter | Qt::AlignLeft, after);
  } else {
    painter->setPen(text_color);
    painter->drawText(text_rect, Qt::AlignVCenter | Qt::AlignLeft, snippet);
  }
  painter->restore();
}
