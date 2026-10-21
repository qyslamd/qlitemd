#ifndef FILE_MARKDOWN_RENDER_SERVICE_H_
#define FILE_MARKDOWN_RENDER_SERVICE_H_

#include <memory>

#include <QObject>
#include <QVector>

#include "markdown_render_tasks.h"

struct MarkdownRenderResult {
  MarkdownRenderTask task;
  QString rendered;
  // ok 表示引擎调用是否成功。Highlight 失败时 rendered 仍是可用的纯转义
  // 兜底，不要仅凭 ok=false 丢弃 rendered
  bool ok = false;
  bool cancelled = false;
  bool engine_load_failed = false;
  // 该池所有 worker 都加载失败；单个 worker 失败不算
  bool engine_all_failed = false;
  bool loaded = false;
  qint64 first_load_ms = 0;
  qint64 elapsed_ms = 0;
  qint64 js_memory_bytes = 0;
  QVector<MarkdownRenderArtifact> artifacts;
};

Q_DECLARE_METATYPE(MarkdownRenderResult)

// 按引擎分池的后台渲染服务。任务只带 payload/key，不碰 MarkdownRenderContext；
// 缓存、提交、视图路由留在 GUI 线程。
//
// 进程级共享池：所有视图复用同一批 worker，任务/结果用 view_id 路由。
// Submit 前应先在 GUI 线程调用 CollectRenderTasks()（其中 MermaidRenderKey
// 会预热字体），否则首次字体度量可能发生在 worker
class MarkdownRenderService : public QObject {
  Q_OBJECT

 public:
  explicit MarkdownRenderService(QObject* parent = nullptr);
  ~MarkdownRenderService() override;

  // 进程级实例；首次调用才创建线程。Instance 会挂 aboutToQuit -> Shutdown
  static MarkdownRenderService& Instance();
  // 幂等关闭：取消正在处理的任务、清队列、有界等待；析构也会调用
  void Shutdown();

  // 一次派发一批任务；view_id/request_id 会覆盖任务里的字段。空批次立即发
  // allFinished。新批次不会自动取消旧批次，调用方先 Cancel()
  void Submit(const QString& view_id, quint64 request_id,
              const QVector<MarkdownRenderTask>& tasks);

  // 取消一整代：排队任务在 worker 取出时跳过，正在处理任务的 QuickJS 被
  // 中断，随后重建 runtime。取消代不再发 taskFinished/allFinished
  void Cancel(const QString& view_id, quint64 request_id);
  // 视图销毁：取消该视图所有正在处理的批次
  void CancelView(const QString& view_id);

  bool busy() const;
  // 各池 worker/队列/处理中的实时快照；未创建的池为零
  MarkdownServiceStatus Status() const;

 signals:
  void taskFinished(const QString& view_id, const MarkdownRenderResult& result);
  void allFinished(const QString& view_id, quint64 request_id);

 public slots:
  // worker -> GUI 的内部回调；宿主不要直接调用
  void OnWorkerResult(MarkdownRenderResult result);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

#endif  // FILE_MARKDOWN_RENDER_SERVICE_H_
