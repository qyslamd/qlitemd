#ifndef FILE_EXAMPLES_MD_VIEWER_DOCUMENTS_PANEL_H_
#define FILE_EXAMPLES_MD_VIEWER_DOCUMENTS_PANEL_H_

#include <QHash>
#include <QSet>
#include <QString>
#include <QWidget>

#include "bookmarks.h"
#include "document_index.h"
#include "text_search.h"

class QLabel;
class QLineEdit;
class QPushButton;
class QStackedWidget;
class QTabWidget;
class QTreeWidget;
class QTreeWidgetItem;

constexpr int kPathRole = Qt::UserRole;
constexpr int kRelativePathRole = Qt::UserRole + 1;
constexpr int kIsDirectoryRole = Qt::UserRole + 2;
constexpr int kMissingRole = Qt::UserRole + 3;
constexpr int kSearchHitRole = Qt::UserRole + 4;
constexpr int kSizeRole = Qt::UserRole + 5;
constexpr int kCountRole = Qt::UserRole + 6;
constexpr int kBookmarkedRole = Qt::UserRole + 7;
constexpr int kBookmarkDisplayPathRole = Qt::UserRole + 8;
constexpr int kQueryLengthRole = Qt::UserRole + 9;

// 左侧栏：文档树 / 书签 / 搜索结果三个页签。只负责渲染与发信号，
// 根目录、书签集合、搜索任务都由 MdViewerWindow 编排。
class DocumentsPanel : public QWidget {
  Q_OBJECT

 public:
  enum class Tab { kDocuments = 0, kBookmarks = 1, kSearch = 2 };

  explicit DocumentsPanel(QWidget* parent = nullptr);

  void SetRoot(const DocumentScanResult& result);
  void SetRootMissing(const QString& root, const QString& reason);
  void SetFilter(const QString& text);
  void SetCurrentFile(const QString& absolute_path);
  void SetBookmarks(const QVector<Bookmark>& bookmarks,
                    const QString& current_root);
  void BeginSearch(const QString& query);
  void AppendSearchHits(const QVector<SearchHit>& hits, int scanned_files,
                        int total_files);
  void EndSearch(bool truncated, bool canceled);
  void SetSearchError(const QString& message);
  void ShowTab(Tab tab);
  Tab currentTab() const;

 signals:
  void fileActivated(const QString& absolute_path);
  void bookmarkActivated(const QString& absolute_path);
  void bookmarkRemoveRequested(const QString& absolute_path);
  // 文档树行内星标：切换该文件的收藏状态
  void bookmarkToggleRequested(const QString& absolute_path);
  void searchHitActivated(const SearchHit& hit);
  void cancelSearchRequested();
  void chooseRootRequested();

 private:
  void initUi();
  void initSignals();
  void BuildTree(const DocumentScanResult& result);
  void ApplyFilter();
  void ShowEmptyPage(const QString& message);
  void UpdateFooter();
  void UpdateDocumentBookmarkRoles();

  QTabWidget* tabs_ = nullptr;
  QLineEdit* filter_edit_ = nullptr;
  QStackedWidget* document_stack_ = nullptr;
  QTreeWidget* document_tree_ = nullptr;
  QTreeWidget* bookmark_tree_ = nullptr;
  QTreeWidget* search_tree_ = nullptr;
  QLabel* search_summary_ = nullptr;
  QLabel* search_empty_ = nullptr;
  QHash<QString, QTreeWidgetItem*> search_groups_;
  QHash<QString, int> search_group_counts_;
  QString current_query_;
  int search_total_hits_ = 0;
  int search_total_files_ = 0;
  int search_scanned_files_ = 0;
  QWidget* empty_page_ = nullptr;
  QLabel* empty_label_ = nullptr;
  QPushButton* choose_root_button_ = nullptr;
  QLabel* footer_label_ = nullptr;
  QPushButton* cancel_button_ = nullptr;
  QString current_file_;
  QString filter_text_;
  QSet<QString> bookmarked_paths_;
  // delegate 在 mouseRelease 里先切换收藏，随后视图还会发 itemClicked；
  // 这里记一笔，避免同一次点击又把文档打开
  bool star_click_pending_ = false;
  DocumentScanResult last_result_;
};

#endif  // FILE_EXAMPLES_MD_VIEWER_DOCUMENTS_PANEL_H_
