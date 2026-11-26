#ifndef FILE_APPS_QT_MD_VIEWER_MARKDOWN_OUTLINE_WIDGET_H_
#define FILE_APPS_QT_MD_VIEWER_MARKDOWN_OUTLINE_WIDGET_H_

#include <QHash>
#include <QTreeWidget>
#include <QVector>

#include "markdown_types.h"

class QTreeWidgetItem;

// 文档大纲：标题按层级缩进，点击发锚点。纯数据驱动——不依赖 MarkdownView，
// 滚动联动由窗口接。
//
// 不设任何样式表：配色交给应用的 Fluent 样式体系
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

#endif  // FILE_APPS_QT_MD_VIEWER_MARKDOWN_OUTLINE_WIDGET_H_
