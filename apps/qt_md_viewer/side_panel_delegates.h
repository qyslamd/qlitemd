#ifndef FILE_APPS_QT_MD_VIEWER_SIDE_PANEL_DELEGATES_H_
#define FILE_APPS_QT_MD_VIEWER_SIDE_PANEL_DELEGATES_H_

#include <functional>

#include <QString>
#include <QStyledItemDelegate>

// 文档树单列自绘：目录计数、文件大小、行内星标都在同一行里。
// 主题切换时由页面调 set_dark；图标在 paint 时按标志现取。
class DocumentTreeDelegate : public QStyledItemDelegate {
 public:
  using ToggleCallback = std::function<void(const QString&)>;

  DocumentTreeDelegate(QWidget* parent, ToggleCallback toggle);

  void set_dark(bool dark) { dark_ = dark; }

  void paint(QPainter* painter, const QStyleOptionViewItem& option,
             const QModelIndex& index) const override;
  QSize sizeHint(const QStyleOptionViewItem& option,
                 const QModelIndex& index) const override;
  bool editorEvent(QEvent* event, QAbstractItemModel* model,
                   const QStyleOptionViewItem& option,
                   const QModelIndex& index) override;

 private:
  ToggleCallback toggle_;
  bool dark_ = false;
};

// 书签双行卡片：实心星 + 文件名 + 相对路径
class BookmarkDelegate : public QStyledItemDelegate {
 public:
  using QStyledItemDelegate::QStyledItemDelegate;

  void set_dark(bool dark) { dark_ = dark; }

  void paint(QPainter* painter, const QStyleOptionViewItem& option,
             const QModelIndex& index) const override;
  QSize sizeHint(const QStyleOptionViewItem& option,
                 const QModelIndex& index) const override;

 private:
  bool dark_ = false;
};

// 命中项自绘：行号 + 片段，关键词用淡化的高亮底色标出
class SearchResultDelegate : public QStyledItemDelegate {
 public:
  using QStyledItemDelegate::QStyledItemDelegate;

  void paint(QPainter* painter, const QStyleOptionViewItem& option,
             const QModelIndex& index) const override;
};

#endif  // FILE_APPS_QT_MD_VIEWER_SIDE_PANEL_DELEGATES_H_
