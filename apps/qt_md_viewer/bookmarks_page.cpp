#include "bookmarks_page.h"

#include <QAbstractItemView>
#include <QFileInfo>
#include <QLabel>
#include <QMenu>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

#include "side_panel_delegates.h"
#include "side_panel_roles.h"

BookmarksPage::BookmarksPage(QWidget* parent) : QWidget(parent) {
  initUi();
  initSignals();
}

void BookmarksPage::initUi() {
  setObjectName(QStringLiteral("bookmarksPage"));
  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(4, 8, 4, 8);
  auto* hint =
      new QLabel(tr("Press Ctrl+D to bookmark the current document"), this);
  hint->setWordWrap(true);
  layout->addWidget(hint);
  bookmark_tree_ = new QTreeWidget(this);
  bookmark_tree_->setObjectName(QStringLiteral("bookmarkTree"));
  bookmark_tree_->setColumnCount(1);
  bookmark_tree_->setHeaderHidden(true);
  bookmark_tree_->setRootIsDecorated(false);
  bookmark_tree_->setAlternatingRowColors(false);
  bookmark_tree_->setUniformRowHeights(false);
  bookmark_tree_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
  bookmark_tree_->setContextMenuPolicy(Qt::CustomContextMenu);
  tree_delegate_ = new BookmarkDelegate(bookmark_tree_);
  bookmark_tree_->setItemDelegate(tree_delegate_);
  layout->addWidget(bookmark_tree_, 1);
}

void BookmarksPage::initSignals() {
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
        QAction* remove = menu.addAction(tr("Remove Bookmark"));
        if (menu.exec(bookmark_tree_->viewport()->mapToGlobal(pos)) == remove) {
          emit bookmarkRemoveRequested(path);
        }
      });
}

void BookmarksPage::SetBookmarks(const QVector<Bookmark>& bookmarks,
                                 const QString& current_root) {
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
}

void BookmarksPage::SetThemeDark(bool dark) {
  if (theme_dark_ == dark) {
    return;
  }
  theme_dark_ = dark;
  if (tree_delegate_ != nullptr) {
    tree_delegate_->set_dark(dark);
  }
  // delegate 的图标是 paint 时现取的，重绘即可
  bookmark_tree_->viewport()->update();
}
