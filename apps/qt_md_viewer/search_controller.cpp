#include "search_controller.h"

#include <QFileInfo>
#include <QTimer>

namespace {

constexpr int kDocumentFindDelayMs = 180;
constexpr int kWorkspaceSearchDelayMs = 300;

}  // namespace

SearchController::SearchController(QObject* parent) : QObject(parent) {
  find_timer_ = new QTimer(this);
  find_timer_->setSingleShot(true);
  find_timer_->setInterval(kDocumentFindDelayMs);
  connect(find_timer_, &QTimer::timeout, this, [this] {
    if (!pending_document_query_.isEmpty()) {
      emit documentFindRequested(pending_document_query_, false);
    }
  });

  search_timer_ = new QTimer(this);
  search_timer_->setSingleShot(true);
  search_timer_->setInterval(kWorkspaceSearchDelayMs);
  connect(search_timer_, &QTimer::timeout, this,
          &SearchController::StartWorkspaceScan);

  search_ = new TextSearch(this);
  connect(search_, &TextSearch::batchReady, this,
          [this](quint64 request_id, const QVector<SearchHit>& hits,
                 int scanned_files, int total_files) {
            if (!IsActiveRequest(request_id)) {
              return;
            }
            emit workspaceSearchBatch(hits, scanned_files, total_files);
          });
  connect(search_, &TextSearch::finished, this,
          [this](quint64 request_id, bool truncated, bool canceled) {
            if (!IsActiveRequest(request_id)) {
              return;
            }
            active_id_.reset();
            emit workspaceSearchFinished(truncated, canceled);
          });
  connect(search_, &TextSearch::failed, this,
          [this](quint64 request_id, const QString& error) {
            if (!IsActiveRequest(request_id)) {
              return;
            }
            active_id_.reset();
            emit workspaceSearchFailed(error);
          });
}

void SearchController::ScheduleDocumentFind(const QString& query) {
  find_timer_->stop();
  pending_document_query_ = query;
  if (!query.isEmpty()) {
    find_timer_->start();
  }
}

void SearchController::FindDocumentNow(const QString& query, bool backward) {
  find_timer_->stop();
  if (!query.isEmpty()) {
    emit documentFindRequested(query, backward);
  }
}

void SearchController::StopDocumentFind() {
  find_timer_->stop();
  pending_document_query_.clear();
}

void SearchController::ScheduleWorkspaceSearch(const QString& root,
                                               const QString& query) {
  ResetWorkspaceSearch();
  // 短查询回到空态；宿主据此清空结果页
  emit workspaceSearchReset(query.size() < kSearchMinQueryLength ? QString()
                                                                 : query);
  if (query.size() < kSearchMinQueryLength) {
    return;
  }
  pending_root_ = root;
  pending_query_ = query;
  search_timer_->start();
}

void SearchController::SearchWorkspaceNow(const QString& root,
                                          const QString& query) {
  search_timer_->stop();
  pending_root_ = root;
  pending_query_ = query;
  StartWorkspaceScan();
}

void SearchController::StartWorkspaceScan() {
  if (pending_query_.size() < kSearchMinQueryLength) {
    return;
  }
  if (pending_root_.isEmpty() || !QFileInfo(pending_root_).isDir()) {
    emit workspaceSearchFailed(QStringLiteral("先打开一个目录"));
    return;
  }
  active_query_ = pending_query_;
  active_id_ = search_->Start(pending_root_, active_query_);
}

void SearchController::CancelWorkspaceSearch() { search_->Cancel(); }

void SearchController::ResetWorkspaceSearch() {
  search_timer_->stop();
  search_->Cancel();
  active_id_.reset();
  active_query_.clear();
  pending_query_.clear();
  pending_root_.clear();
}

bool SearchController::IsActiveRequest(quint64 request_id) const {
  return active_id_.has_value() && request_id == *active_id_;
}
