#include "documents_panel.h"

#include <algorithm>
#include <functional>
#include <utility>

#include <QAbstractItemModel>
#include <QAbstractItemView>
#include <QEvent>
#include <QFileInfo>
#include <QFont>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QHash>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPalette>
#include <QPushButton>
#include <QStackedWidget>
#include <QStyle>
#include <QStyleOptionViewItem>
#include <QStyledItemDelegate>
#include <QTabBar>
#include <QTabWidget>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QTreeWidgetItemIterator>
#include <QVBoxLayout>
#include <QVariant>

#include "fluentui3styleproperties.h"

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

// 文档树单列自绘：目录计数、文件大小、行内星标都在同一行里
class DocumentTreeDelegate : public QStyledItemDelegate {
 public:
  using ToggleCallback = std::function<void(const QString&)>;

  DocumentTreeDelegate(QWidget* parent, ToggleCallback toggle)
      : QStyledItemDelegate(parent), toggle_(std::move(toggle)) {}

  void paint(QPainter* painter, const QStyleOptionViewItem& option,
             const QModelIndex& index) const override {
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
    const QIcon& icon = is_directory ? folder_icon_ : file_icon_;
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
    const QRect right_rect(right_edge - right_width, opt.rect.top(),
                           right_width, opt.rect.height());
    const QRect name_rect(
        icon_rect.right() + 6, opt.rect.top(),
        std::max(0, right_rect.left() - icon_rect.right() - 12),
        opt.rect.height());

    if (show_star) {
      const QIcon& star = bookmarked ? star_filled_ : star_outline_;
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
    painter->drawText(right_rect, Qt::AlignVCenter | Qt::AlignRight,
                      right_text);
    painter->restore();
  }

  QSize sizeHint(const QStyleOptionViewItem& option,
                 const QModelIndex& index) const override {
    Q_UNUSED(index);
    return QSize(
        0, std::max(kTreeRowHeight, QFontMetrics(option.font).height() + 10));
  }

  bool editorEvent(QEvent* event, QAbstractItemModel* model,
                   const QStyleOptionViewItem& option,
                   const QModelIndex& index) override {
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

 private:
  ToggleCallback toggle_;
  const QIcon folder_icon_{QStringLiteral(":/resources/svg/folder.svg")};
  const QIcon file_icon_{QStringLiteral(":/resources/svg/file.svg")};
  const QIcon star_outline_{QStringLiteral(":/resources/svg/star-outline.svg")};
  const QIcon star_filled_{QStringLiteral(":/resources/svg/star-filled.svg")};
};

// 书签双行卡片：实心星 + 文件名 + 相对路径
class BookmarkDelegate : public QStyledItemDelegate {
 public:
  using QStyledItemDelegate::QStyledItemDelegate;

  void paint(QPainter* painter, const QStyleOptionViewItem& option,
             const QModelIndex& index) const override {
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
    const QIcon& star = missing ? star_missing_ : star_filled_;
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

  QSize sizeHint(const QStyleOptionViewItem& option,
                 const QModelIndex& index) const override {
    Q_UNUSED(index);
    const int content_height = QFontMetrics(option.font).height() +
                               QFontMetrics(SmallFont(option.font)).height() +
                               12;
    return QSize(0, std::max(kBookmarkRowHeight, content_height));
  }

 private:
  const QIcon star_filled_{QStringLiteral(":/resources/svg/star-filled.svg")};
  const QIcon star_missing_{QStringLiteral(":/resources/svg/star-outline.svg")};
};

// 命中项自绘：行号 + 片段，关键词用淡化的高亮底色标出
class SearchResultDelegate : public QStyledItemDelegate {
 public:
  using QStyledItemDelegate::QStyledItemDelegate;

  void paint(QPainter* painter, const QStyleOptionViewItem& option,
             const QModelIndex& index) const override {
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
          QRect(text_rect.left() + before_width, text_rect.top() + 2,
                match_width, text_rect.height() - 4),
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
};

bool ApplyFilterToItem(QTreeWidgetItem* item, const QString& needle) {
  bool self_matches = needle.isEmpty();
  if (!self_matches) {
    self_matches = item->data(0, kRelativePathRole)
                       .toString()
                       .contains(needle, Qt::CaseInsensitive);
  }
  bool child_visible = false;
  for (int i = 0; i < item->childCount(); ++i) {
    if (ApplyFilterToItem(item->child(i), needle)) {
      child_visible = true;
    }
  }
  const bool visible = self_matches || child_visible;
  item->setHidden(!visible);
  if (!needle.isEmpty() && visible) {
    // 命中项和它的祖先都展开，否则过滤后还要手动逐层点开
    item->setExpanded(true);
  }
  return visible;
}

QString RootDisplayName(const QString& root) {
  const QString name = QFileInfo(root).fileName();
  return name.isEmpty() ? root : name;
}

void SetEntryRoles(QTreeWidgetItem* item, const DocumentEntry& entry) {
  item->setData(0, kPathRole, entry.absolute_path);
  item->setData(0, kRelativePathRole, entry.relative_path);
  item->setData(0, kIsDirectoryRole, entry.is_dir);
  item->setToolTip(0, entry.absolute_path);
}

}  // namespace

DocumentsPanel::DocumentsPanel(QWidget* parent) : QWidget(parent) {
  initUi();
  initSignals();
}

void DocumentsPanel::initUi() {
  auto* outer = new QVBoxLayout(this);
  outer->setContentsMargins(0, 0, 0, 0);
  outer->setSpacing(0);

  // 过滤框在三个页签上方，与原型一致；它仍然只作用于文档树
  auto* filter_wrap = new QWidget(this);
  auto* filter_layout = new QHBoxLayout(filter_wrap);
  filter_layout->setContentsMargins(8, 8, 8, 4);
  filter_layout->setSpacing(0);
  filter_edit_ = new QLineEdit(filter_wrap);
  filter_edit_->setObjectName(QStringLiteral("documentFilterEdit"));
  filter_edit_->setPlaceholderText(QStringLiteral("筛选文件与路径"));
  filter_edit_->setClearButtonEnabled(true);
  filter_layout->addWidget(filter_edit_);
  outer->addWidget(filter_wrap);

  tabs_ = new QTabWidget(this);
  tabs_->setObjectName(QStringLiteral("sidebarTabs"));
  tabs_->setDocumentMode(true);
  tabs_->setIconSize(QSize(15, 15));
  // FluentUI 自带的分段页签；WinUI3 变体要求每段至少 130px，
  // 320px 侧栏放不下三段，改用 Slide 变体（图标+文字仍保留）
  tabs_->tabBar()->setProperty(TabBarStyleProperty, Segmented_Slide);
  tabs_->tabBar()->setExpanding(true);
  tabs_->tabBar()->setUsesScrollButtons(false);
  tabs_->tabBar()->setElideMode(Qt::ElideRight);

  auto* documents_page = new QWidget(tabs_);
  auto* documents_layout = new QVBoxLayout(documents_page);
  documents_layout->setContentsMargins(4, 4, 4, 8);
  documents_layout->setSpacing(6);

  document_stack_ = new QStackedWidget(documents_page);
  document_stack_->setObjectName(QStringLiteral("documentStack"));
  document_tree_ = new QTreeWidget(document_stack_);
  document_tree_->setObjectName(QStringLiteral("documentTree"));
  document_tree_->setColumnCount(1);
  document_tree_->setHeaderHidden(true);
  document_tree_->setRootIsDecorated(true);
  document_tree_->setIndentation(16);
  document_tree_->setAlternatingRowColors(false);
  document_tree_->setUniformRowHeights(true);
  document_tree_->setMouseTracking(true);
  document_tree_->viewport()->setMouseTracking(true);
  document_tree_->setItemDelegate(
      new DocumentTreeDelegate(document_tree_, [this](const QString& path) {
        star_click_pending_ = true;
        emit bookmarkToggleRequested(path);
      }));
  document_stack_->addWidget(document_tree_);

  empty_page_ = new QWidget(document_stack_);
  empty_page_->setObjectName(QStringLiteral("sidebarEmptyPage"));
  auto* empty_layout = new QVBoxLayout(empty_page_);
  empty_layout->setContentsMargins(16, 16, 16, 16);
  empty_layout->setSpacing(10);
  empty_layout->addStretch(1);
  empty_label_ = new QLabel(empty_page_);
  empty_label_->setObjectName(QStringLiteral("sidebarEmptyLabel"));
  empty_label_->setWordWrap(true);
  empty_label_->setAlignment(Qt::AlignCenter);
  empty_layout->addWidget(empty_label_);
  choose_root_button_ =
      new QPushButton(QStringLiteral("重新选择目录"), empty_page_);
  choose_root_button_->setObjectName(QStringLiteral("sidebarChooseRootButton"));
  empty_layout->addWidget(choose_root_button_, 0, Qt::AlignCenter);
  empty_layout->addStretch(1);
  document_stack_->addWidget(empty_page_);
  documents_layout->addWidget(document_stack_, 1);

  tabs_->addTab(documents_page,
                QIcon(QStringLiteral(":/resources/svg/folder.svg")),
                QStringLiteral("文档"));

  auto* bookmarks_page = new QWidget(tabs_);
  bookmarks_page->setObjectName(QStringLiteral("bookmarksPage"));
  auto* bookmarks_layout = new QVBoxLayout(bookmarks_page);
  bookmarks_layout->setContentsMargins(4, 8, 4, 8);
  auto* bookmarks_hint =
      new QLabel(QStringLiteral("用 Ctrl+D 收藏当前文档"), bookmarks_page);
  bookmarks_hint->setWordWrap(true);
  bookmarks_layout->addWidget(bookmarks_hint);
  bookmark_tree_ = new QTreeWidget(bookmarks_page);
  bookmark_tree_->setObjectName(QStringLiteral("bookmarkTree"));
  bookmark_tree_->setColumnCount(1);
  bookmark_tree_->setHeaderHidden(true);
  bookmark_tree_->setRootIsDecorated(false);
  bookmark_tree_->setAlternatingRowColors(false);
  bookmark_tree_->setUniformRowHeights(false);
  bookmark_tree_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
  bookmark_tree_->setContextMenuPolicy(Qt::CustomContextMenu);
  bookmark_tree_->setItemDelegate(new BookmarkDelegate(bookmark_tree_));
  bookmarks_layout->addWidget(bookmark_tree_, 1);
  tabs_->addTab(bookmarks_page,
                QIcon(QStringLiteral(":/resources/svg/star-outline.svg")),
                QStringLiteral("书签"));

  auto* search_page = new QWidget(tabs_);
  search_page->setObjectName(QStringLiteral("searchPage"));
  auto* search_layout = new QVBoxLayout(search_page);
  search_layout->setContentsMargins(4, 8, 4, 8);
  search_layout->setSpacing(6);
  search_summary_ = new QLabel(search_page);
  search_summary_->setObjectName(QStringLiteral("searchSummaryLabel"));
  search_summary_->setWordWrap(true);
  search_summary_->setText(QStringLiteral("输入关键词开始搜索"));
  search_layout->addWidget(search_summary_);
  search_tree_ = new QTreeWidget(search_page);
  search_tree_->setObjectName(QStringLiteral("searchResultTree"));
  search_tree_->setColumnCount(1);
  search_tree_->setHeaderHidden(true);
  search_tree_->setRootIsDecorated(true);
  search_tree_->setIndentation(12);
  search_tree_->setAlternatingRowColors(false);
  search_tree_->setUniformRowHeights(true);
  search_tree_->setMouseTracking(true);
  search_tree_->viewport()->setMouseTracking(true);
  search_tree_->setItemDelegate(new SearchResultDelegate(search_tree_));
  search_layout->addWidget(search_tree_, 1);
  search_empty_ = new QLabel(search_page);
  search_empty_->setObjectName(QStringLiteral("searchEmptyLabel"));
  search_empty_->setWordWrap(true);
  search_empty_->setAlignment(Qt::AlignCenter);
  search_empty_->hide();
  search_layout->addWidget(search_empty_);
  tabs_->addTab(search_page,
                QIcon(QStringLiteral(":/resources/svg/search.svg")),
                QStringLiteral("搜索"));

  outer->addWidget(tabs_, 1);

  auto* footer = new QWidget(this);
  footer->setObjectName(QStringLiteral("sidebarFooter"));
  auto* footer_layout = new QHBoxLayout(footer);
  footer_layout->setContentsMargins(8, 4, 8, 4);
  footer_layout->setSpacing(6);
  footer_label_ = new QLabel(footer);
  footer_label_->setObjectName(QStringLiteral("sidebarFooterLabel"));
  footer_layout->addWidget(footer_label_, 1);
  cancel_button_ = new QPushButton(QStringLiteral("取消"), footer);
  cancel_button_->setObjectName(QStringLiteral("sidebarCancelButton"));
  cancel_button_->hide();
  footer_layout->addWidget(cancel_button_);
  outer->addWidget(footer);
}

void DocumentsPanel::initSignals() {
  connect(filter_edit_, &QLineEdit::textChanged, this,
          &DocumentsPanel::SetFilter);
  connect(document_tree_, &QTreeWidget::itemClicked, this,
          [this](QTreeWidgetItem* item, int) {
            if (star_click_pending_) {
              star_click_pending_ = false;
              return;
            }
            if (item->data(0, kIsDirectoryRole).toBool()) {
              item->setExpanded(!item->isExpanded());
              return;
            }
            emit fileActivated(item->data(0, kPathRole).toString());
          });
  connect(choose_root_button_, &QPushButton::clicked, this,
          &DocumentsPanel::chooseRootRequested);
  connect(bookmark_tree_, &QTreeWidget::itemClicked, this,
          [this](QTreeWidgetItem* item, int) {
            emit bookmarkActivated(item->data(0, kPathRole).toString());
          });
  connect(
      bookmark_tree_, &QTreeWidget::customContextMenuRequested, this,
      [this](const QPoint& pos) {
        QTreeWidgetItem* item = bookmark_tree_->itemAt(pos);
        if (item == nullptr) {
          return;
        }
        // exec 的嵌套事件循环里 watcher 可能刷新书签页并删掉 item，
        // 路径必须在进菜单前取下来
        const QString path = item->data(0, kPathRole).toString();
        QMenu menu(this);
        QAction* remove = menu.addAction(QStringLiteral("移除书签"));
        if (menu.exec(bookmark_tree_->viewport()->mapToGlobal(pos)) == remove) {
          emit bookmarkRemoveRequested(path);
        }
      });
  connect(search_tree_, &QTreeWidget::itemClicked, this,
          [this](QTreeWidgetItem* item, int) {
            if (item->parent() == nullptr) {
              item->setExpanded(!item->isExpanded());
              return;
            }
            emit searchHitActivated(
                item->data(0, kSearchHitRole).value<SearchHit>());
          });
  connect(cancel_button_, &QPushButton::clicked, this,
          &DocumentsPanel::cancelSearchRequested);
  connect(tabs_, &QTabWidget::currentChanged, this,
          [this](int) { UpdateFooter(); });
}

void DocumentsPanel::SetRoot(const DocumentScanResult& result) {
  last_result_ = result;
  if (!result.root_exists) {
    ShowEmptyPage(QStringLiteral("目录不存在或不可读"));
    return;
  }

  // 重建前记住展开状态与当前文件；刷新后尽量回到原处
  QStringList expanded_paths;
  if (document_tree_->topLevelItemCount() > 0) {
    for (QTreeWidgetItemIterator it(document_tree_); *it; ++it) {
      if ((*it)->isExpanded()) {
        expanded_paths.append((*it)->data(0, kPathRole).toString());
      }
    }
  }

  document_tree_->clear();
  if (result.markdown_file_count == 0) {
    ShowEmptyPage(QStringLiteral("该目录中没有 Markdown 文档"));
    UpdateFooter();
    return;
  }

  BuildTree(result);
  for (QTreeWidgetItemIterator it(document_tree_); *it; ++it) {
    const QString path = (*it)->data(0, kPathRole).toString();
    if (expanded_paths.contains(path)) {
      (*it)->setExpanded(true);
    }
  }
  if (!current_file_.isEmpty()) {
    SetCurrentFile(current_file_);
  }
  ApplyFilter();
  document_stack_->setCurrentWidget(document_tree_);
  UpdateFooter();
}

void DocumentsPanel::SetRootMissing(const QString& root,
                                    const QString& reason) {
  current_file_.clear();
  document_tree_->clear();
  last_result_ = DocumentScanResult();
  last_result_.root = root;
  last_result_.root_exists = false;
  UpdateFooter();
  empty_label_->setToolTip(root);
  ShowEmptyPage(reason.isEmpty() ? QStringLiteral("目录不存在或不可读")
                                 : reason);
}

void DocumentsPanel::BuildTree(const DocumentScanResult& result) {
  // 每个目录显示其子树里的 Markdown 篇数
  QHash<QString, int> directory_counts;
  for (const DocumentEntry& entry : result.entries) {
    if (entry.is_dir) {
      continue;
    }
    QString parent = QFileInfo(entry.relative_path).path();
    while (!parent.isEmpty() && parent != QStringLiteral(".")) {
      ++directory_counts[parent];
      parent = QFileInfo(parent).path();
    }
  }

  auto* root_item = new QTreeWidgetItem(document_tree_);
  root_item->setText(0, RootDisplayName(result.root));
  root_item->setData(0, kPathRole, result.root);
  root_item->setData(0, kRelativePathRole, QString());
  root_item->setData(0, kIsDirectoryRole, true);
  root_item->setData(0, kCountRole, result.markdown_file_count);
  root_item->setToolTip(0, result.root);

  QHash<QString, QTreeWidgetItem*> directory_items;
  for (const DocumentEntry& entry : result.entries) {
    if (!entry.is_dir) {
      continue;
    }
    const QString parent_relative = QFileInfo(entry.relative_path).path();
    QTreeWidgetItem* parent =
        parent_relative == QStringLiteral(".")
            ? root_item
            : directory_items.value(parent_relative, root_item);
    auto* item = new QTreeWidgetItem(parent);
    item->setText(0, QFileInfo(entry.relative_path).fileName());
    item->setData(0, kCountRole,
                  directory_counts.value(entry.relative_path, 0));
    SetEntryRoles(item, entry);
    directory_items.insert(entry.relative_path, item);
  }

  for (const DocumentEntry& entry : result.entries) {
    if (entry.is_dir) {
      continue;
    }
    const QString parent_relative = QFileInfo(entry.relative_path).path();
    QTreeWidgetItem* parent =
        parent_relative == QStringLiteral(".")
            ? root_item
            : directory_items.value(parent_relative, root_item);
    auto* item = new QTreeWidgetItem(parent);
    item->setText(0, QFileInfo(entry.relative_path).fileName());
    item->setData(0, kSizeRole, entry.size);
    item->setData(0, kBookmarkedRole,
                  bookmarked_paths_.contains(entry.absolute_path));
    SetEntryRoles(item, entry);
  }
  root_item->setExpanded(true);
}

void DocumentsPanel::ApplyFilter() {
  for (int i = 0; i < document_tree_->topLevelItemCount(); ++i) {
    ApplyFilterToItem(document_tree_->topLevelItem(i), filter_text_);
  }
}

void DocumentsPanel::SetFilter(const QString& text) {
  filter_text_ = text;
  ApplyFilter();
}

void DocumentsPanel::SetCurrentFile(const QString& absolute_path) {
  current_file_ = absolute_path;
  for (QTreeWidgetItemIterator it(document_tree_); *it; ++it) {
    if ((*it)->data(0, kPathRole).toString() != absolute_path) {
      continue;
    }
    for (QTreeWidgetItem* parent = (*it)->parent(); parent != nullptr;
         parent = parent->parent()) {
      parent->setExpanded(true);
    }
    document_tree_->setCurrentItem(*it);
    document_tree_->scrollToItem(*it);
    return;
  }
}

void DocumentsPanel::SetBookmarks(const QVector<Bookmark>& bookmarks,
                                  const QString& current_root) {
  bookmarked_paths_.clear();
  for (const Bookmark& bookmark : bookmarks) {
    bookmarked_paths_.insert(bookmark.path);
  }
  bookmark_tree_->clear();
  for (const Bookmark& bookmark : bookmarks) {
    auto* item = new QTreeWidgetItem(bookmark_tree_);
    item->setText(0, QFileInfo(bookmark.path).fileName());
    item->setData(0, kBookmarkDisplayPathRole,
                  DisplayBookmarkPath(bookmark, current_root));
    item->setToolTip(0, bookmark.path);
    item->setData(0, kPathRole, bookmark.path);
    item->setData(0, kMissingRole, !QFileInfo(bookmark.path).isFile());
  }
  UpdateDocumentBookmarkRoles();
}

void DocumentsPanel::UpdateDocumentBookmarkRoles() {
  if (document_tree_ == nullptr) {
    return;
  }
  for (QTreeWidgetItemIterator it(document_tree_); *it; ++it) {
    if ((*it)->data(0, kIsDirectoryRole).toBool()) {
      continue;
    }
    const QString path = (*it)->data(0, kPathRole).toString();
    (*it)->setData(0, kBookmarkedRole, bookmarked_paths_.contains(path));
  }
  document_tree_->viewport()->update();
}

void DocumentsPanel::BeginSearch(const QString& query) {
  current_query_ = query;
  search_groups_.clear();
  search_group_counts_.clear();
  search_tree_->clear();
  search_total_hits_ = 0;
  search_total_files_ = 0;
  search_scanned_files_ = 0;
  search_empty_->hide();
  if (query.isEmpty()) {
    search_summary_->setText(QStringLiteral("输入关键词开始搜索"));
    cancel_button_->hide();
  } else {
    search_summary_->setText(QStringLiteral("正在搜索…"));
    cancel_button_->show();
  }
  UpdateFooter();
}

void DocumentsPanel::AppendSearchHits(const QVector<SearchHit>& hits,
                                      int scanned_files, int total_files) {
  search_scanned_files_ = scanned_files;
  search_total_files_ = total_files;
  for (const SearchHit& hit : hits) {
    QTreeWidgetItem* group = search_groups_.value(hit.file_path);
    if (group == nullptr) {
      group = new QTreeWidgetItem(search_tree_);
      group->setData(0, kPathRole, hit.file_path);
      group->setExpanded(true);
      QFont font = group->font(0);
      font.setBold(true);
      group->setFont(0, font);
      search_groups_.insert(hit.file_path, group);
    }
    const int count = ++search_group_counts_[hit.file_path];
    group->setText(0,
                   QStringLiteral("%1 (%2)").arg(hit.relative_path).arg(count));
    auto* child = new QTreeWidgetItem(group);
    child->setData(0, kSearchHitRole, QVariant::fromValue(hit));
    child->setData(0, kQueryLengthRole, current_query_.size());
    ++search_total_hits_;
  }
  search_summary_->setText(QStringLiteral("在 %1 篇文档中搜索 “%2” · %3 处命中")
                               .arg(search_total_files_)
                               .arg(current_query_)
                               .arg(search_total_hits_));
  UpdateFooter();
}

void DocumentsPanel::EndSearch(bool truncated, bool canceled) {
  cancel_button_->hide();
  QString notice;
  if (canceled) {
    notice = QStringLiteral(" · 已取消（部分结果）");
  } else if (truncated) {
    notice = QStringLiteral(" · 已截断");
  }
  search_summary_->setText(
      QStringLiteral("在 %1 篇文档中搜索 “%2” · %3 处命中%4")
          .arg(search_total_files_)
          .arg(current_query_)
          .arg(search_total_hits_)
          .arg(notice));
  if (search_total_hits_ == 0) {
    search_empty_->setText(canceled ? QStringLiteral("搜索已取消")
                                    : QStringLiteral("未找到匹配"));
    search_empty_->show();
  }
  UpdateFooter();
}

void DocumentsPanel::SetSearchError(const QString& message) {
  cancel_button_->hide();
  search_summary_->setText(message);
  search_empty_->setText(message);
  search_empty_->show();
  UpdateFooter();
}

void DocumentsPanel::UpdateFooter() {
  const int index = tabs_->currentIndex();
  if (index == static_cast<int>(Tab::kSearch)) {
    if (cancel_button_->isVisible()) {
      footer_label_->setText(QStringLiteral("已扫描 %1/%2")
                                 .arg(search_scanned_files_)
                                 .arg(search_total_files_));
    } else if (!current_query_.isEmpty()) {
      footer_label_->setText(
          QStringLiteral("共 %1 处命中").arg(search_total_hits_));
    } else {
      footer_label_->clear();
    }
    return;
  }
  if (index == static_cast<int>(Tab::kDocuments)) {
    footer_label_->setText(
        QStringLiteral("共 %1 篇文档").arg(last_result_.markdown_file_count));
  } else {
    footer_label_->clear();
  }
}

void DocumentsPanel::ShowTab(Tab tab) {
  tabs_->setCurrentIndex(static_cast<int>(tab));
  UpdateFooter();
}

DocumentsPanel::Tab DocumentsPanel::currentTab() const {
  return static_cast<Tab>(tabs_->currentIndex());
}

void DocumentsPanel::ShowEmptyPage(const QString& message) {
  empty_label_->setText(message);
  document_stack_->setCurrentWidget(empty_page_);
}
