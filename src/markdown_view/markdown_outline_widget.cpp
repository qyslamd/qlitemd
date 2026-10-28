#include "markdown_outline_widget.h"

namespace {

// 条目携带的数据：锚点给宿主回跳，层级用于搭父子关系
constexpr int kAnchorRole = Qt::UserRole;
constexpr int kLevelRole = Qt::UserRole + 1;
constexpr int kIndentPx = 14;

}  // namespace

MarkdownOutlineWidget::MarkdownOutlineWidget(QWidget* parent)
    : QTreeWidget(parent) {
  setHeaderHidden(true);
  // 根装饰是展开箭头；层级缩进靠父子项保留
  setRootIsDecorated(false);
  setUniformRowHeights(true);
  setIndentation(kIndentPx);
  setSelectionMode(QAbstractItemView::SingleSelection);
  connect(this, &QTreeWidget::itemClicked, this,
          &MarkdownOutlineWidget::ActivateItem);
  connect(this, &QTreeWidget::itemActivated, this,
          &MarkdownOutlineWidget::ActivateItem);
}

void MarkdownOutlineWidget::SetHeadings(
    const QVector<MarkdownHeading>& headings) {
  clear();
  items_.clear();

  // path 是当前打开的祖先链；新标题的父项是最后一个层级更浅的标题。
  // 跳级（h1 → h3）也按"最近的更浅标题"处理
  QVector<QTreeWidgetItem*> path;
  for (const MarkdownHeading& heading : headings) {
    while (!path.isEmpty() &&
           path.last()->data(0, kLevelRole).toInt() >= heading.level) {
      path.removeLast();
    }
    QTreeWidgetItem* parent = path.isEmpty() ? nullptr : path.last();
    auto* item = parent != nullptr ? new QTreeWidgetItem(parent)
                                   : new QTreeWidgetItem(this);
    item->setText(0, heading.text);
    item->setData(0, kAnchorRole, heading.anchor);
    item->setData(0, kLevelRole, heading.level);
    item->setToolTip(0, heading.text);
    // 箭头藏起来了，但子项仍要展开才可见
    item->setExpanded(true);
    items_.insert(heading.anchor, item);
    path.append(item);
  }
}

void MarkdownOutlineWidget::SetCurrentAnchor(const QString& anchor) {
  const auto it = items_.constFind(anchor);
  QTreeWidgetItem* item = it == items_.constEnd() ? nullptr : it.value();
  // 滚动时高频调用：目标没变就直接返回，别每次重建空 QModelIndex
  if (currentItem() == item) {
    return;
  }
  setCurrentItem(item);
}

void MarkdownOutlineWidget::ActivateItem(QTreeWidgetItem* item) {
  if (item == nullptr) {
    return;
  }
  const QString anchor = item->data(0, kAnchorRole).toString();
  if (!anchor.isEmpty()) {
    emit headingActivated(anchor);
  }
}
