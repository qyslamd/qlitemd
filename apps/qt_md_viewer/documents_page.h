#ifndef FILE_APPS_QT_MD_VIEWER_DOCUMENTS_PAGE_H_
#define FILE_APPS_QT_MD_VIEWER_DOCUMENTS_PAGE_H_

#include <QSet>
#include <QString>
#include <QWidget>

#include "document_index.h"

class QLabel;
class QLineEdit;
class QPushButton;
class QStackedWidget;
class QTreeWidget;
class QTreeWidgetItem;
class DocumentTreeDelegate;

// 侧栏的“文档”页：筛选框 + 递归文档树 / 空态。只负责渲染与发信号，
// 根目录、收藏集合由宿主编排。
class DocumentsPage : public QWidget {
  Q_OBJECT

 public:
  explicit DocumentsPage(QWidget* parent = nullptr);

  void SetRoot(const DocumentScanResult& result);
  void SetRootMissing(const QString& root, const QString& reason);
  void SetFilter(const QString& text);
  void SetCurrentFile(const QString& absolute_path);
  // 只用来画行内星标；书签列表由 BookmarksPage 自己维护
  void SetBookmarkedPaths(const QSet<QString>& paths);
  void SetThemeDark(bool dark);

 signals:
  // new_tab：Ctrl+点击或中键；普通点击替换当前标签
  void fileActivated(const QString& absolute_path, bool new_tab);
  // 文档树行内星标：切换该文件的收藏状态
  void bookmarkToggleRequested(const QString& absolute_path);
  void chooseRootRequested();

 protected:
  bool eventFilter(QObject* watched, QEvent* event) override;

 private:
  void initUi();
  void initSignals();
  void BuildTree(const DocumentScanResult& result);
  void ApplyFilter();
  void ShowEmptyPage(const QString& message);
  void UpdateBookmarkRoles();

  QLineEdit* filter_edit_ = nullptr;
  QStackedWidget* document_stack_ = nullptr;
  QTreeWidget* document_tree_ = nullptr;
  QWidget* empty_page_ = nullptr;
  QLabel* empty_label_ = nullptr;
  QPushButton* choose_root_button_ = nullptr;
  DocumentTreeDelegate* tree_delegate_ = nullptr;
  QString current_file_;
  QString filter_text_;
  QSet<QString> bookmarked_paths_;
  // delegate 在 mouseRelease 里先切换收藏，随后视图还会发 itemClicked；
  // 这里记一笔，避免同一次点击又把文档打开
  bool star_click_pending_ = false;
  // 文档树点击时记下 Ctrl 状态；中键自己发信号后吞掉随后的 itemClicked
  bool ctrl_click_ = false;
  bool suppress_item_click_ = false;
  bool theme_dark_ = false;
  DocumentScanResult last_result_;
};

#endif  // FILE_APPS_QT_MD_VIEWER_DOCUMENTS_PAGE_H_
