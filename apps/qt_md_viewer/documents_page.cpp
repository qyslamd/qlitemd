#include "documents_page.h"

#include <QEvent>
#include <QFileInfo>
#include <QFont>
#include <QHBoxLayout>
#include <QHash>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPushButton>
#include <QStackedWidget>
#include <QStringList>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QTreeWidgetItemIterator>
#include <QVBoxLayout>

#include "side_panel_delegates.h"
#include "side_panel_roles.h"

namespace {

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

DocumentsPage::DocumentsPage(QWidget* parent) : QWidget(parent) {
  initUi();
  initSignals();
}

void DocumentsPage::initUi() {
  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(4, 4, 4, 8);
  layout->setSpacing(6);

  // 筛选框只筛树，不搜正文；搜索页另有自己的工作区搜索框
  auto* filter_wrap = new QWidget(this);
  auto* filter_layout = new QHBoxLayout(filter_wrap);
  filter_layout->setContentsMargins(0, 0, 0, 0);
  filter_layout->setSpacing(0);
  filter_edit_ = new QLineEdit(filter_wrap);
  filter_edit_->setObjectName(QStringLiteral("documentFilterEdit"));
  filter_edit_->setPlaceholderText(tr("Filter files and paths"));
  filter_edit_->setClearButtonEnabled(true);
  filter_layout->addWidget(filter_edit_);
  layout->addWidget(filter_wrap);

  document_stack_ = new QStackedWidget(this);
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
  // Ctrl+点击与中键新开标签需要事件里的 modifiers/button；itemClicked
  // 两个都拿不到
  document_tree_->viewport()->installEventFilter(this);
  tree_delegate_ =
      new DocumentTreeDelegate(document_tree_, [this](const QString& path) {
        star_click_pending_ = true;
        emit bookmarkToggleRequested(path);
      });
  document_tree_->setItemDelegate(tree_delegate_);
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
  choose_root_button_ = new QPushButton(tr("Choose Folder Again"), empty_page_);
  choose_root_button_->setObjectName(QStringLiteral("sidebarChooseRootButton"));
  empty_layout->addWidget(choose_root_button_, 0, Qt::AlignCenter);
  empty_layout->addStretch(1);
  document_stack_->addWidget(empty_page_);
  layout->addWidget(document_stack_, 1);
}

void DocumentsPage::initSignals() {
  connect(filter_edit_, &QLineEdit::textChanged, this,
          &DocumentsPage::SetFilter);
  connect(choose_root_button_, &QPushButton::clicked, this,
          &DocumentsPage::chooseRootRequested);
  connect(document_tree_, &QTreeWidget::itemClicked, this,
          [this](QTreeWidgetItem* item, int) {
            if (suppress_item_click_) {
              suppress_item_click_ = false;
              return;
            }
            if (star_click_pending_) {
              star_click_pending_ = false;
              return;
            }
            if (item->data(0, kIsDirectoryRole).toBool()) {
              item->setExpanded(!item->isExpanded());
              return;
            }
            emit fileActivated(item->data(0, kPathRole).toString(),
                               ctrl_click_);
          });
}

bool DocumentsPage::eventFilter(QObject* watched, QEvent* event) {
  if (document_tree_ != nullptr && watched == document_tree_->viewport()) {
    if (event->type() == QEvent::MouseButtonPress) {
      auto* mouse = static_cast<QMouseEvent*>(event);
      // 中键吞掉自己的 itemClicked 后，标志要留到下一次左键按下再清；
      // 否则会连下一条左键点击一起吞掉
      if (mouse->button() == Qt::LeftButton) {
        suppress_item_click_ = false;
      }
      ctrl_click_ = (mouse->modifiers() & Qt::ControlModifier) != 0;
      if (mouse->button() == Qt::MiddleButton) {
        QTreeWidgetItem* item = document_tree_->itemAt(mouse->pos());
        if (item != nullptr && !item->data(0, kIsDirectoryRole).toBool()) {
          suppress_item_click_ = true;
          emit fileActivated(item->data(0, kPathRole).toString(), true);
          return true;
        }
      }
    } else if (event->type() == QEvent::MouseButtonRelease) {
      auto* mouse = static_cast<QMouseEvent*>(event);
      if (mouse->button() == Qt::MiddleButton) {
        return true;
      }
    }
  }
  return QWidget::eventFilter(watched, event);
}

void DocumentsPage::SetRoot(const DocumentScanResult& result) {
  last_result_ = result;
  if (!result.root_exists) {
    ShowEmptyPage(tr("Folder does not exist or is not readable"));
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
    ShowEmptyPage(tr("No Markdown documents in this folder"));
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
}

void DocumentsPage::SetRootMissing(const QString& root, const QString& reason) {
  current_file_.clear();
  document_tree_->clear();
  last_result_ = DocumentScanResult();
  last_result_.root = root;
  last_result_.root_exists = false;
  empty_label_->setToolTip(root);
  ShowEmptyPage(reason.isEmpty()
                    ? tr("Folder does not exist or is not readable")
                    : reason);
}

void DocumentsPage::BuildTree(const DocumentScanResult& result) {
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

void DocumentsPage::ApplyFilter() {
  for (int i = 0; i < document_tree_->topLevelItemCount(); ++i) {
    ApplyFilterToItem(document_tree_->topLevelItem(i), filter_text_);
  }
}

void DocumentsPage::SetFilter(const QString& text) {
  filter_text_ = text;
  ApplyFilter();
}

void DocumentsPage::SetCurrentFile(const QString& absolute_path) {
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

void DocumentsPage::SetBookmarkedPaths(const QSet<QString>& paths) {
  bookmarked_paths_ = paths;
  UpdateBookmarkRoles();
}

void DocumentsPage::UpdateBookmarkRoles() {
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

void DocumentsPage::ShowEmptyPage(const QString& message) {
  empty_label_->setText(message);
  document_stack_->setCurrentWidget(empty_page_);
}

void DocumentsPage::SetThemeDark(bool dark) {
  if (theme_dark_ == dark) {
    return;
  }
  theme_dark_ = dark;
  if (tree_delegate_ != nullptr) {
    tree_delegate_->set_dark(dark);
  }
  document_tree_->viewport()->update();
}
