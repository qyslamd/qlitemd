#include "render_service.h"

#include <algorithm>
#include <atomic>
#include <memory>
#include <vector>

#include <QCoreApplication>
#include <QDebug>
#include <QElapsedTimer>
#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <QPointer>
#include <QSet>
#include <QThread>
#include <QWaitCondition>

#include "html_escape.h"
#include "markdown_engine_state.h"

namespace {

constexpr int kHighlightWorkers = 2;
constexpr int kFormulaWorkers = 2;
// mermaid 单张最慢，多图文档靠它把首屏压下来
constexpr int kMermaidWorkers = 4;
constexpr int kShutdownTimeoutMs = 30000;

MarkdownRenderResult RenderOne(MarkdownRenderTask task,
                               HighlightEngineState& highlight,
                               FormulaEngineState& formula,
                               MermaidEngineState& mermaid) {
  MarkdownRenderResult result;
  result.task = task;
  if (task.cancel_flag != nullptr &&
      task.cancel_flag->load(std::memory_order_relaxed)) {
    result.cancelled = true;
    return result;
  }

  JsRuntime* runtime = nullptr;
  switch (task.kind) {
    case MarkdownRenderKind::Highlight:
      runtime = &highlight.runtime;
      break;
    case MarkdownRenderKind::Formula:
      runtime = &formula.runtime;
      break;
    case MarkdownRenderKind::Mermaid:
      runtime = &mermaid.runtime;
      break;
  }
  runtime->SetInterruptFlag(task.cancel_flag.get());
  QElapsedTimer render_clock;
  render_clock.start();

  switch (task.kind) {
    case MarkdownRenderKind::Highlight: {
      bool failed = false;
      result.rendered = RenderHighlightOnEngine(
          highlight, task.payload, task.language, task.diagnostics, &failed);
      result.ok = !failed;
      if (result.rendered.isEmpty()) {
        // 未注册语言或引擎失败都退纯转义，与同步入口一致
        result.rendered = EscapeHtml(task.payload);
      }
      break;
    }
    case MarkdownRenderKind::Formula:
      result.rendered = RenderFormulaOnEngine(
          formula, task.payload, task.display, task.formula_color,
          task.diagnostics, &result.artifacts);
      result.ok = !result.rendered.isEmpty();
      break;
    case MarkdownRenderKind::Mermaid:
      result.rendered =
          RenderMermaidOnEngine(mermaid, task.payload, task.theme,
                                task.diagnostics, &result.artifacts);
      result.ok = !result.rendered.isEmpty();
      break;
  }

  result.elapsed_ms = render_clock.elapsed();
  result.js_memory_bytes = runtime->MemoryBytes();
  if (task.cancel_flag != nullptr &&
      task.cancel_flag->load(std::memory_order_relaxed)) {
    // QuickJS 被中断后 runtime 状态不可继续用；下一次任务必须重载
    result.cancelled = true;
    result.ok = false;
    result.rendered.clear();
    runtime->ResetForReload();
  } else {
    const MarkdownEngineStatus status = runtime->status();
    result.engine_load_failed = status.permanently_failed;
    result.loaded = status.loaded;
    result.first_load_ms = status.first_load_ms;
  }
  runtime->SetInterruptFlag(nullptr);
  return result;
}

class RenderWorkerThread : public QThread {
 public:
  explicit RenderWorkerThread(MarkdownRenderService* service)
      : service_(service) {
    // QuickJS 16MB 配额需要至少 32MB 宿主栈
    setStackSize(32 * 1024 * 1024);
  }

  void Enqueue(const MarkdownRenderTask& task) {
    QMutexLocker lock(&mutex_);
    tasks_.append(task);
    condition_.wakeOne();
  }

  void Stop() {
    QMutexLocker lock(&mutex_);
    stopping_ = true;
    // 排队任务的结果反正会被丢弃，别让析构等它们跑完
    tasks_.clear();
    condition_.wakeAll();
  }

  // 析构等待超时后与 service 断开，避免 worker 再回调已销毁对象
  void Detach() {
    QMutexLocker lock(&mutex_);
    service_ = nullptr;
  }

  int QueuedCount() const {
    QMutexLocker lock(&mutex_);
    return tasks_.size();
  }

  bool Processing() const {
    QMutexLocker lock(&mutex_);
    return processing_;
  }

 protected:
  void run() override {
    HighlightEngineState highlight;
    FormulaEngineState formula;
    MermaidEngineState mermaid;
    while (true) {
      MarkdownRenderTask task;
      {
        QMutexLocker lock(&mutex_);
        while (tasks_.isEmpty() && !stopping_) {
          condition_.wait(&mutex_);
        }
        if (stopping_) {
          break;
        }
        task = tasks_.takeFirst();
        processing_ = true;
      }
      MarkdownRenderResult result =
          RenderOne(task, highlight, formula, mermaid);
      // 投递与 Detach 共用一把锁：投递完成后析构才会把 service_ 置空；
      // 已投递事件由 Qt 在 receiver 销毁时移除
      {
        QMutexLocker lock(&mutex_);
        processing_ = false;
        if (service_ != nullptr) {
          QMetaObject::invokeMethod(
              service_,
              [service = service_, result]() {
                service->OnWorkerResult(result);
              },
              Qt::QueuedConnection);
        }
      }
    }
  }

 private:
  MarkdownRenderService* service_ = nullptr;
  mutable QMutex mutex_;
  QWaitCondition condition_;
  QVector<MarkdownRenderTask> tasks_;
  bool stopping_ = false;
  bool processing_ = false;
};

int PickWorker(const std::vector<bool>& dead, int* next) {
  const int size = static_cast<int>(dead.size());
  if (size <= 0) {
    return -1;
  }
  for (int attempt = 0; attempt < size; ++attempt) {
    const int index = (*next)++ % size;
    if (!dead[index]) {
      return index;
    }
  }
  return -1;
}

bool AllDead(const std::vector<bool>& dead) {
  return !dead.empty() && std::all_of(dead.begin(), dead.end(),
                                      [](bool value) { return value; });
}

QString BatchKey(const QString& view_id, quint64 request_id) {
  return view_id + QChar(0x1f) + QString::number(request_id);
}

}  // namespace

struct MarkdownRenderService::Impl {
  std::vector<std::unique_ptr<RenderWorkerThread>> highlight_workers;
  std::vector<std::unique_ptr<RenderWorkerThread>> formula_workers;
  std::vector<std::unique_ptr<RenderWorkerThread>> mermaid_workers;
  std::vector<bool> highlight_dead;
  std::vector<bool> formula_dead;
  std::vector<bool> mermaid_dead;
  int next_highlight = 0;
  int next_formula = 0;
  int next_mermaid = 0;
  struct BatchInfo {
    QString view_id;
    quint64 request_id = 0;
  };
  // 按 (view_id, request_id) 记未完成数；每批归零各发一次 allFinished
  QHash<QString, int> pending_by_batch;
  QHash<QString, BatchInfo> batch_info;
  QSet<QString> cancelled_batches;
  QHash<QString, std::shared_ptr<std::atomic<bool>>> cancel_flags;
  qint64 highlight_js_memory = 0;
  qint64 formula_js_memory = 0;
  qint64 mermaid_js_memory = 0;
  bool shutdown = false;
};

MarkdownRenderService& MarkdownRenderService::Instance() {
  static MarkdownRenderService service;
  static bool connected = false;
  if (!connected) {
    if (QCoreApplication* app = QCoreApplication::instance()) {
      QObject::connect(app, &QCoreApplication::aboutToQuit, &service,
                       &MarkdownRenderService::Shutdown, Qt::UniqueConnection);
      connected = true;
    }
  }
  return service;
}

MarkdownRenderService::MarkdownRenderService(QObject* parent)
    : QObject(parent), impl_(new Impl) {
  qRegisterMetaType<MarkdownRenderResult>("MarkdownRenderResult");
  auto create_pool = [this](
                         std::vector<std::unique_ptr<RenderWorkerThread>>* pool,
                         int count) {
    for (int i = 0; i < count; ++i) {
      auto worker = std::make_unique<RenderWorkerThread>(this);
      worker->start();
      pool->push_back(std::move(worker));
    }
  };
  create_pool(&impl_->highlight_workers, kHighlightWorkers);
  create_pool(&impl_->formula_workers, kFormulaWorkers);
  create_pool(&impl_->mermaid_workers, kMermaidWorkers);
  impl_->highlight_dead.assign(kHighlightWorkers, false);
  impl_->formula_dead.assign(kFormulaWorkers, false);
  impl_->mermaid_dead.assign(kMermaidWorkers, false);
}

MarkdownRenderService::~MarkdownRenderService() { Shutdown(); }

void MarkdownRenderService::Shutdown() {
  if (impl_->shutdown) {
    return;
  }
  impl_->shutdown = true;
  // 先打断所有正在处理的任务；worker 取到取消任务会立即返回并重建 runtime
  for (auto it = impl_->cancel_flags.constBegin();
       it != impl_->cancel_flags.constEnd(); ++it) {
    it.value()->store(true, std::memory_order_relaxed);
  }
  auto stop_pool = [](std::vector<std::unique_ptr<RenderWorkerThread>>* pool) {
    for (const auto& worker : *pool) {
      worker->Stop();
    }
  };
  stop_pool(&impl_->highlight_workers);
  stop_pool(&impl_->formula_workers);
  stop_pool(&impl_->mermaid_workers);

  // 三个池共享一个总预算，避免串行 3 × 30s
  QElapsedTimer deadline;
  deadline.start();
  auto wait_pool =
      [&deadline](std::vector<std::unique_ptr<RenderWorkerThread>>* pool) {
        for (auto& worker : *pool) {
          const qint64 remaining = kShutdownTimeoutMs - deadline.elapsed();
          if (remaining <= 0 ||
              !worker->wait(static_cast<unsigned long>(remaining))) {
            qWarning() << "markdown: render worker did not stop within"
                       << kShutdownTimeoutMs << "ms; detaching";
            worker->Detach();
            worker.release();  // 有意泄漏：不能销毁仍在运行的 QThread
          }
        }
      };
  wait_pool(&impl_->highlight_workers);
  wait_pool(&impl_->formula_workers);
  wait_pool(&impl_->mermaid_workers);
}

void MarkdownRenderService::Submit(const QString& view_id, quint64 request_id,
                                   const QVector<MarkdownRenderTask>& tasks) {
  const QString batch = BatchKey(view_id, request_id);
  if (impl_->shutdown) {
    return;
  }
  if (tasks.isEmpty()) {
    // 同一 request 已有批次正在处理时不重复收尾
    if (impl_->pending_by_batch.contains(batch)) {
      return;
    }
    // 排队发，避免宿主在 allFinished 槽里重入 Submit
    QMetaObject::invokeMethod(
        this,
        [this, view_id, request_id]() {
          emit allFinished(view_id, request_id);
        },
        Qt::QueuedConnection);
    return;
  }
  auto flag = impl_->cancel_flags.value(batch);
  if (flag == nullptr) {
    flag = std::make_shared<std::atomic<bool>>(false);
    impl_->cancel_flags.insert(batch, flag);
  }
  impl_->cancelled_batches.remove(batch);
  impl_->batch_info.insert(batch, Impl::BatchInfo{view_id, request_id});
  impl_->pending_by_batch[batch] += tasks.size();

  for (MarkdownRenderTask task : tasks) {
    task.view_id = view_id;
    task.request_id = request_id;
    task.cancel_flag = flag;
    int index = -1;
    if (task.kind == MarkdownRenderKind::Highlight) {
      index = PickWorker(impl_->highlight_dead, &impl_->next_highlight);
      if (index >= 0) {
        task.worker_id = index;
        impl_->highlight_workers[index]->Enqueue(task);
      }
    } else if (task.kind == MarkdownRenderKind::Formula) {
      index = PickWorker(impl_->formula_dead, &impl_->next_formula);
      if (index >= 0) {
        task.worker_id = index;
        impl_->formula_workers[index]->Enqueue(task);
      }
    } else {
      Q_ASSERT(task.kind == MarkdownRenderKind::Mermaid);
      index = PickWorker(impl_->mermaid_dead, &impl_->next_mermaid);
      if (index >= 0) {
        task.worker_id = index;
        impl_->mermaid_workers[index]->Enqueue(task);
      }
    }
    if (index < 0) {
      // 该引擎没有存活 worker：合成一次失败结果，批次仍能收尾
      MarkdownRenderResult result;
      result.task = task;
      result.engine_load_failed = true;
      result.engine_all_failed = true;
      QMetaObject::invokeMethod(
          this, [this, result]() { OnWorkerResult(result); },
          Qt::QueuedConnection);
    }
  }
}

void MarkdownRenderService::Cancel(const QString& view_id, quint64 request_id) {
  const QString batch = BatchKey(view_id, request_id);
  const auto it = impl_->cancel_flags.find(batch);
  if (it == impl_->cancel_flags.end()) {
    return;  // 尚未派发/已排空，别留下永不清理的取消标记
  }
  it.value()->store(true, std::memory_order_relaxed);
  impl_->cancelled_batches.insert(batch);
}

void MarkdownRenderService::CancelView(const QString& view_id) {
  for (auto it = impl_->batch_info.constBegin();
       it != impl_->batch_info.constEnd(); ++it) {
    if (it.value().view_id != view_id) {
      continue;
    }
    const auto flag = impl_->cancel_flags.find(it.key());
    if (flag != impl_->cancel_flags.end()) {
      flag.value()->store(true, std::memory_order_relaxed);
    }
    impl_->cancelled_batches.insert(it.key());
  }
}

bool MarkdownRenderService::busy() const {
  return !impl_->pending_by_batch.isEmpty();
}

MarkdownServiceStatus MarkdownRenderService::Status() const {
  auto make_pool =
      [](const QString& name,
         const std::vector<std::unique_ptr<RenderWorkerThread>>& workers,
         const std::vector<bool>& dead, qint64 js_memory) {
        MarkdownPoolStatus status;
        status.name = name;
        status.workers = static_cast<int>(workers.size());
        for (bool failed : dead) {
          if (failed) {
            ++status.failed;
          }
        }
        status.alive = status.workers - status.failed;
        for (const auto& worker : workers) {
          status.queued += worker->QueuedCount();
          if (worker->Processing()) {
            ++status.processing;
          }
        }
        status.js_memory_bytes = js_memory;
        return status;
      };

  MarkdownServiceStatus status;
  status.highlight =
      make_pool(QStringLiteral("高亮"), impl_->highlight_workers,
                impl_->highlight_dead, impl_->highlight_js_memory);
  status.formula = make_pool(QStringLiteral("公式"), impl_->formula_workers,
                             impl_->formula_dead, impl_->formula_js_memory);
  status.mermaid = make_pool(QStringLiteral("Mermaid"), impl_->mermaid_workers,
                             impl_->mermaid_dead, impl_->mermaid_js_memory);
  status.pending_batches = impl_->pending_by_batch.size();
  for (int pending : impl_->pending_by_batch) {
    status.pending_tasks += pending;
  }
  return status;
}

void MarkdownRenderService::OnWorkerResult(MarkdownRenderResult result) {
  const QString view_id = result.task.view_id;
  const quint64 request_id = result.task.request_id;
  const QString batch = BatchKey(view_id, request_id);
  const int worker_id = result.task.worker_id;
  if (result.engine_load_failed && worker_id >= 0) {
    std::vector<bool>* dead = nullptr;
    if (result.task.kind == MarkdownRenderKind::Highlight) {
      dead = &impl_->highlight_dead;
    } else if (result.task.kind == MarkdownRenderKind::Formula) {
      dead = &impl_->formula_dead;
    } else {
      dead = &impl_->mermaid_dead;
    }
    if (worker_id < static_cast<int>(dead->size())) {
      (*dead)[worker_id] = true;
    }
    result.engine_all_failed = AllDead(*dead);
  }

  qint64* js_memory = nullptr;
  if (result.task.kind == MarkdownRenderKind::Highlight) {
    js_memory = &impl_->highlight_js_memory;
  } else if (result.task.kind == MarkdownRenderKind::Formula) {
    js_memory = &impl_->formula_js_memory;
  } else {
    js_memory = &impl_->mermaid_js_memory;
  }
  *js_memory = result.js_memory_bytes;

  const bool cancelled =
      result.cancelled || impl_->cancelled_batches.contains(batch);
  bool batch_done = false;
  const auto it = impl_->pending_by_batch.find(batch);
  if (it != impl_->pending_by_batch.end()) {
    if (--it.value() <= 0) {
      impl_->pending_by_batch.erase(it);
      batch_done = true;
    }
  }
  if (cancelled) {
    if (batch_done) {
      impl_->cancelled_batches.remove(batch);
      impl_->cancel_flags.remove(batch);
      impl_->batch_info.remove(batch);
    }
    return;  // 取消代不向宿主发结果
  }

  // 槽里可能同步销毁 service，emit 之后不能再碰 impl_
  const QPointer<MarkdownRenderService> guard(this);
  emit taskFinished(view_id, result);
  if (guard.isNull() || !batch_done) {
    return;
  }
  // taskFinished 槽里可能又给同一 request 加了新批次，别提前收尾
  if (impl_->pending_by_batch.contains(batch)) {
    return;
  }
  impl_->cancel_flags.remove(batch);
  impl_->batch_info.remove(batch);
  QMetaObject::invokeMethod(
      this,
      [this, view_id, request_id]() { emit allFinished(view_id, request_id); },
      Qt::QueuedConnection);
}
