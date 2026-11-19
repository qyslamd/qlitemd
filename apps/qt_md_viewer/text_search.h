#ifndef FILE_EXAMPLES_MD_VIEWER_TEXT_SEARCH_H_
#define FILE_EXAMPLES_MD_VIEWER_TEXT_SEARCH_H_

#include <atomic>
#include <functional>

#include <QMetaType>
#include <QObject>
#include <QString>
#include <QVector>

class QThread;

constexpr int kSearchMinQueryLength = 2;
constexpr int kSearchMaxHitsPerFile = 50;
constexpr int kSearchMaxTotalHits = 2000;
constexpr int kSearchSnippetLength = 200;

struct SearchHit {
  QString file_path;      // 绝对路径
  QString relative_path;  // 相对根，'/'
  int line = 0;           // 1-based
  QString snippet;
  int match_offset = 0;  // 命中在 snippet 内的起点
  int file_ordinal = 0;  // 该文件内第几个命中（0-based，跳转用）
};

Q_DECLARE_METATYPE(SearchHit)

struct SnippetResult {
  QString text;
  int match_offset = 0;
};

SnippetResult MakeSearchSnippet(const QString& line, int match_start,
                                int query_length,
                                int max_length = kSearchSnippetLength);

struct FileScanResult {
  QVector<SearchHit> hits;
  bool canceled = false;
};

FileScanResult ScanFileForQuery(const QString& file_path,
                                const QString& relative_path,
                                const QString& query, int max_hits,
                                const std::atomic_bool* cancel);

using SearchBatchCallback =
    std::function<void(const QVector<SearchHit>&, int scanned, int total)>;

struct RootScanResult {
  QVector<SearchHit> hits;
  int scanned_files = 0;
  int total_files = 0;
  bool truncated = false;
  bool canceled = false;
};

RootScanResult ScanRootForQuery(const QString& root, const QString& query,
                                const std::atomic_bool* cancel,
                                const SearchBatchCallback& on_batch);

// QThread 门面：信号从 worker 线程发出，调用方在自己的线程收 queued 信号。
class TextSearch : public QObject {
  Q_OBJECT

 public:
  explicit TextSearch(QObject* parent = nullptr);
  ~TextSearch() override;

  // 返回本轮请求 id；后续信号都带同一个 id，调用方据此丢弃旧扫描的排队信号
  quint64 Start(const QString& root, const QString& query);
  void Cancel();
  bool IsRunning() const;

 signals:
  void batchReady(quint64 request_id, const QVector<SearchHit>& hits,
                  int scanned_files, int total_files);
  void finished(quint64 request_id, bool truncated, bool canceled);
  void failed(quint64 request_id, const QString& error);

 private:
  std::atomic_bool cancel_{false};
  std::atomic_bool running_{false};
  QThread* thread_ = nullptr;
  quint64 next_request_id_ = 0;
};

#endif  // FILE_EXAMPLES_MD_VIEWER_TEXT_SEARCH_H_
