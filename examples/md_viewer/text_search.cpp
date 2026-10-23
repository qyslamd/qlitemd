#include "text_search.h"

#include <algorithm>

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QStringList>
#include <QThread>

#include "document_index.h"

namespace {

constexpr int kReadChunkSize = 64 * 1024;
constexpr int kMaxLineBytes = 1024 * 1024;
constexpr int kSnippetContextBefore = 60;
constexpr int kSearchBatchHits = 32;
constexpr int kSearchBatchIntervalMs = 100;

}  // namespace

SnippetResult MakeSearchSnippet(const QString& line, int match_start,
                                int query_length, int max_length) {
  SnippetResult result;
  const int length = std::max(1, max_length);
  if (line.size() <= length) {
    result.text = line;
    result.match_offset = match_start;
    return result;
  }

  int start = std::max(0, match_start - kSnippetContextBefore);
  if (start + length > line.size()) {
    start = line.size() - length;
  }
  if (match_start + query_length > start + length) {
    start = std::max(0, match_start + query_length - length);
  }
  result.text = line.mid(start, length);
  result.match_offset = match_start - start;
  if (start > 0) {
    result.text.prepend(QChar(0x2026));
    ++result.match_offset;
  }
  if (start + length < line.size()) {
    result.text.append(QChar(0x2026));
  }
  return result;
}

FileScanResult ScanFileForQuery(const QString& file_path,
                                const QString& relative_path,
                                const QString& query, int max_hits,
                                const std::atomic_bool* cancel) {
  FileScanResult result;
  if (query.isEmpty() || max_hits <= 0) {
    return result;
  }
  QFile file(file_path);
  if (!file.open(QIODevice::ReadOnly)) {
    return result;
  }
  const QString absolute_path = QFileInfo(file_path).absoluteFilePath();
  QByteArray line_buffer;
  int line_number = 0;
  bool line_open = false;
  int ordinal = 0;

  auto process_piece = [&](QByteArray bytes, bool complete) -> bool {
    if (cancel != nullptr && cancel->load()) {
      result.canceled = true;
      return false;
    }
    if (!line_open) {
      ++line_number;
    }
    line_open = !complete;
    if (line_number == 1 && bytes.startsWith("\xEF\xBB\xBF")) {
      bytes.remove(0, 3);
    }
    if (bytes.endsWith('\r')) {
      bytes.chop(1);
    }
    const QString line = QString::fromUtf8(bytes);
    const int match = line.indexOf(query, 0, Qt::CaseInsensitive);
    if (match < 0) {
      return true;
    }
    SearchHit hit;
    hit.file_path = absolute_path;
    hit.relative_path = relative_path;
    hit.line = line_number;
    const SnippetResult snippet = MakeSearchSnippet(line, match, query.size());
    hit.snippet = snippet.text;
    hit.match_offset = snippet.match_offset;
    hit.file_ordinal = ordinal++;
    result.hits.append(hit);
    return result.hits.size() < max_hits;
  };

  while (!file.atEnd()) {
    if (cancel != nullptr && cancel->load()) {
      result.canceled = true;
      return result;
    }
    const QByteArray chunk = file.read(kReadChunkSize);
    if (chunk.isEmpty()) {
      break;
    }
    line_buffer.append(chunk);
    int newline = line_buffer.indexOf('\n');
    while (newline >= 0) {
      const QByteArray line = line_buffer.left(newline);
      line_buffer.remove(0, newline + 1);
      if (!process_piece(line, true)) {
        return result;
      }
      newline = line_buffer.indexOf('\n');
    }
    // 超长行：按同一行号的片段处理，内存不随文件增长
    if (line_buffer.size() >= kMaxLineBytes) {
      if (!process_piece(line_buffer, false)) {
        return result;
      }
      line_buffer.clear();
    }
  }
  if (!line_buffer.isEmpty()) {
    process_piece(line_buffer, true);
  }
  return result;
}

RootScanResult ScanRootForQuery(const QString& root, const QString& query,
                                const std::atomic_bool* cancel,
                                const SearchBatchCallback& on_batch) {
  RootScanResult result;
  const QStringList files = CollectMarkdownFiles(root);
  result.total_files = files.size();
  if (on_batch) {
    on_batch({}, 0, result.total_files);
  }

  QVector<SearchHit> pending;
  QElapsedTimer timer;
  timer.start();
  auto emit_piece = [&](bool force) {
    while (pending.size() >= kSearchBatchHits) {
      const QVector<SearchHit> piece = pending.mid(0, kSearchBatchHits);
      pending.remove(0, kSearchBatchHits);
      result.hits.append(piece);
      if (on_batch) {
        on_batch(piece, result.scanned_files, result.total_files);
      }
      timer.restart();
    }
    if (pending.isEmpty()) {
      return;
    }
    if (!force && timer.elapsed() < kSearchBatchIntervalMs) {
      return;
    }
    const QVector<SearchHit> piece = pending;
    pending.clear();
    result.hits.append(piece);
    if (on_batch) {
      on_batch(piece, result.scanned_files, result.total_files);
    }
    timer.restart();
  };

  for (const QString& file : files) {
    if (cancel != nullptr && cancel->load()) {
      result.canceled = true;
      emit_piece(true);
      return result;
    }
    const int already = result.hits.size() + pending.size();
    if (already >= kSearchMaxTotalHits) {
      result.truncated = true;
      emit_piece(true);
      return result;
    }
    const int remaining = kSearchMaxTotalHits - already;
    const int max_hits = std::min(kSearchMaxHitsPerFile, remaining);
    const QString relative_path =
        QDir::fromNativeSeparators(QDir(root).relativeFilePath(file));
    const FileScanResult file_result =
        ScanFileForQuery(file, relative_path, query, max_hits, cancel);
    pending.append(file_result.hits);
    ++result.scanned_files;
    emit_piece(false);
    if (file_result.canceled) {
      result.canceled = true;
      emit_piece(true);
      return result;
    }
    if (result.hits.size() + pending.size() >= kSearchMaxTotalHits) {
      result.truncated = true;
      emit_piece(true);
      return result;
    }
  }
  emit_piece(true);
  return result;
}

TextSearch::TextSearch(QObject* parent) : QObject(parent) {
  qRegisterMetaType<QVector<SearchHit>>("QVector<SearchHit>");
}

TextSearch::~TextSearch() {
  cancel_ = true;
  if (thread_ != nullptr) {
    thread_->wait();
    delete thread_;
    thread_ = nullptr;
  }
}

quint64 TextSearch::Start(const QString& root, const QString& query) {
  Cancel();
  if (thread_ != nullptr) {
    thread_->wait();
    delete thread_;
    thread_ = nullptr;
  }
  const quint64 request_id = ++next_request_id_;
  cancel_ = false;
  running_ = true;
  thread_ = QThread::create([this, root, query, request_id] {
    const RootScanResult result =
        ScanRootForQuery(root, query, &cancel_,
                         [this, request_id](const QVector<SearchHit>& hits,
                                            int scanned, int total) {
                           emit batchReady(request_id, hits, scanned, total);
                         });
    running_ = false;
    emit finished(request_id, result.truncated, result.canceled);
  });
  thread_->start();
  return request_id;
}

void TextSearch::Cancel() { cancel_ = true; }

bool TextSearch::IsRunning() const { return running_.load(); }
