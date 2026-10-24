#ifndef FILE_MARKDOWN_MARKDOWN_OUTLINE_WIDGET_H_
#define FILE_MARKDOWN_MARKDOWN_OUTLINE_WIDGET_H_

#include <QHash>
#include <QTreeWidget>
#include <QVector>

#include "markdown_types.h"

class QTreeWidgetItem;

// 文档大纲：标题按层级缩进，点击发锚点。纯数据驱动——不依赖 MarkdownView，
// 滚动联动由宿主接（示例见 examples/md_viewer）。
//
// 不设任何样式表：配色交给宿主的样式体系（md_viewer 的 Fluent 风格直接生效）
class MarkdownOutlineWidget : public QTreeWidget {
  Q_OBJECT

 public:
  explicit MarkdownOutlineWidget(QWidget* parent = nullptr);

  // 重建整棵树；空列表 = 清空
  void SetHeadings(const QVector<MarkdownHeading>& headings);
  // 高亮当前章节；找不到就清掉选中，不移动滚动位置
  void SetCurrentAnchor(const QString& anchor);

 signals:
  void headingActivated(const QString& anchor);

 private:
  void ActivateItem(QTreeWidgetItem* item);

  QHash<QString, QTreeWidgetItem*> items_;
};

#endif  // FILE_MARKDOWN_MARKDOWN_OUTLINE_WIDGET_H_
