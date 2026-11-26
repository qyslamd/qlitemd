#ifndef FILE_APPS_QT_MD_VIEWER_SEARCH_PAGE_H_
#define FILE_APPS_QT_MD_VIEWER_SEARCH_PAGE_H_

#include <QHash>
#include <QString>
#include <QVector>
#include <QWidget>

#include "text_search.h"

class QLabel;
class QLineEdit;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;

// 侧栏的“搜索”页：输入框、进度/取消行与结果树。扫描任务由宿主编排，
// 这里只负责呈现与发信号。
class SearchPage : public QWidget {
  Q_OBJECT

 public:
  explicit SearchPage(QWidget* parent = nullptr);

  QLineEdit* search_edit() const { return search_edit_; }
  // 没有工作区时禁用输入，占位文字改成提示
  void SetSearchAvailable(bool available);
  void BeginSearch(const QString& query);
  void AppendSearchHits(const QVector<SearchHit>& hits, int scanned_files,
                        int total_files);
  void EndSearch(bool truncated, bool canceled);
  void SetSearchError(const QString& message);

 signals:
  void searchTextChanged(const QString& text);
  void searchSubmitted();
  void searchHitActivated(const SearchHit& hit);
  void cancelSearchRequested();

 private:
  void initUi();
  void initSignals();

  QLineEdit* search_edit_ = nullptr;
  QLabel* search_summary_ = nullptr;
  QLabel* search_empty_ = nullptr;
  QPushButton* cancel_button_ = nullptr;
  QTreeWidget* search_tree_ = nullptr;
  QHash<QString, QTreeWidgetItem*> search_groups_;
  QHash<QString, int> search_group_counts_;
  QString current_query_;
  int search_total_hits_ = 0;
  int search_total_files_ = 0;
  int search_scanned_files_ = 0;
};

#endif  // FILE_APPS_QT_MD_VIEWER_SEARCH_PAGE_H_
