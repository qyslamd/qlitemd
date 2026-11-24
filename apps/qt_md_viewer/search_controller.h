#ifndef FILE_APPS_QT_MD_VIEWER_SEARCH_CONTROLLER_H_
#define FILE_APPS_QT_MD_VIEWER_SEARCH_CONTROLLER_H_

#include <optional>

#include <QObject>
#include <QString>
#include <QVector>

#include "text_search.h"

class QTimer;

// 两类搜索的异步状态：当前文档查找的防抖，以及工作区搜索的防抖、
// 请求 id 与过期结果过滤。它不碰 MarkdownView、状态栏和侧栏控件，
// 只发结果信号；UI 由宿主接线。
class SearchController : public QObject {
  Q_OBJECT

 public:
  explicit SearchController(QObject* parent = nullptr);

  // 当前文档查找：非空查询在防抖后发一次；回车走 Now
  void ScheduleDocumentFind(const QString& query);
  void FindDocumentNow(const QString& query, bool backward);
  void StopDocumentFind();

  // 工作区搜索：Schedule 立即重置结果并防抖；Now 跳过防抖
  void ScheduleWorkspaceSearch(const QString& root, const QString& query);
  void SearchWorkspaceNow(const QString& root, const QString& query);
  // 只请求取消，保留当前请求，让 canceled 收尾信号能到 UI
  void CancelWorkspaceSearch();
  // 取消并丢弃当前请求与查询（换根、清空输入、卸载工作区）
  void ResetWorkspaceSearch();
  QString active_query() const { return active_query_; }
  bool IsSearching() const { return active_id_.has_value(); }

 signals:
  void documentFindRequested(const QString& query, bool backward);
  // 查询状态变化（含空查询）时发一次，宿主用它重置结果页
  void workspaceSearchReset(const QString& query);
  void workspaceSearchBatch(const QVector<SearchHit>& hits, int scanned_files,
                            int total_files);
  void workspaceSearchFinished(bool truncated, bool canceled);
  void workspaceSearchFailed(const QString& error);

 private:
  void StartWorkspaceScan();
  bool IsActiveRequest(quint64 request_id) const;

  QTimer* find_timer_ = nullptr;
  QTimer* search_timer_ = nullptr;
  TextSearch* search_ = nullptr;
  QString pending_document_query_;
  QString pending_root_;
  QString pending_query_;
  QString active_query_;
  std::optional<quint64> active_id_;
};

#endif  // FILE_APPS_QT_MD_VIEWER_SEARCH_CONTROLLER_H_
