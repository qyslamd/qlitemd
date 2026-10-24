#include "md_pipeline.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#if defined(_WIN32)
#include <process.h>
#include <windows.h>
#else
#include <pthread.h>
#endif

#include "md_assembler.h"
#include "md_formula.h"
#include "md_hash.h"
#include "md_highlight.h"
#include "md_mermaid.h"
#include "md_parser.h"
#include "md_svg.h"
#include "md_text.h"

namespace {

int64_t ElapsedMs(std::chrono::steady_clock::time_point start) {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now() - start)
      .count();
}

// Worker threads need a platform-specific stack size on Windows (QuickJS).
// POSIX keeps the default stack, matching the previous Qt implementation.
class MdWorkerThread {
 public:
  MdWorkerThread(std::function<void()> body, std::size_t stack_bytes);
  ~MdWorkerThread();
  MdWorkerThread(const MdWorkerThread&) = delete;
  MdWorkerThread& operator=(const MdWorkerThread&) = delete;

  void Join();

 private:
  struct Context {
    std::function<void()> body;
  };
#if defined(_WIN32)
  static unsigned __stdcall ThreadProc(void* raw);
  HANDLE handle_ = nullptr;
#else
  static void* ThreadProc(void* raw);
  pthread_t thread_{};
  bool has_thread_ = false;
#endif
  std::thread fallback_;
};

#if defined(_WIN32)

unsigned __stdcall MdWorkerThread::ThreadProc(void* raw) {
  auto* context = static_cast<Context*>(raw);
  context->body();
  delete context;
  return 0;
}

MdWorkerThread::MdWorkerThread(std::function<void()> body,
                               std::size_t stack_bytes) {
  auto* context = new Context{std::move(body)};
  handle_ = reinterpret_cast<HANDLE>(
      _beginthreadex(nullptr, static_cast<unsigned>(stack_bytes), &ThreadProc,
                     context, 0, nullptr));
  if (handle_ == nullptr) {
    fallback_ = std::thread([context] {
      context->body();
      delete context;
    });
  }
}

#else

void* MdWorkerThread::ThreadProc(void* raw) {
  auto* context = static_cast<Context*>(raw);
  context->body();
  delete context;
  return nullptr;
}

MdWorkerThread::MdWorkerThread(std::function<void()> body,
                               std::size_t /*stack_bytes*/) {
  auto* context = new Context{std::move(body)};
  pthread_attr_t attributes;
  pthread_attr_init(&attributes);
  const int result =
      pthread_create(&thread_, &attributes, &ThreadProc, context);
  pthread_attr_destroy(&attributes);
  if (result != 0) {
    delete context;
    throw std::runtime_error("failed to create worker thread");
  }
  has_thread_ = true;
}

#endif

MdWorkerThread::~MdWorkerThread() { Join(); }

void MdWorkerThread::Join() {
#if defined(_WIN32)
  if (handle_ != nullptr) {
    WaitForSingleObject(handle_, INFINITE);
    CloseHandle(handle_);
    handle_ = nullptr;
  }
#else
  if (has_thread_) {
    pthread_join(thread_, nullptr);
    has_thread_ = false;
  }
#endif
  if (fallback_.joinable()) {
    fallback_.join();
  }
}

// One lane per render kind. Each worker owns its engine state and pulls work
// from a bounded queue; the coordinator blocks on Submit when a lane is full.
template <typename Engine>
class MdTaskLane {
 public:
  using Work = std::function<void(Engine&)>;
  using Factory = std::function<std::unique_ptr<Engine>()>;

  MdTaskLane(std::size_t workers, std::size_t capacity, std::size_t stack_bytes,
             Factory factory)
      : capacity_(std::max<std::size_t>(1, capacity)) {
    workers = std::max<std::size_t>(1, workers);
    for (std::size_t i = 0; i < workers; ++i) {
      threads_.push_back(std::make_unique<MdWorkerThread>(
          [this, factory] { WorkerLoop(factory); }, stack_bytes));
    }
  }

  ~MdTaskLane() {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      stop_ = true;
    }
    work_cv_.notify_all();
    space_cv_.notify_all();
    for (std::unique_ptr<MdWorkerThread>& thread : threads_) {
      thread->Join();
    }
  }

  MdTaskLane(const MdTaskLane&) = delete;
  MdTaskLane& operator=(const MdTaskLane&) = delete;

  bool Submit(Work work) {
    std::unique_lock<std::mutex> lock(mutex_);
    space_cv_.wait(lock, [this] { return stop_ || queue_.size() < capacity_; });
    if (stop_) {
      return false;
    }
    queue_.push_back(std::move(work));
    ++queued_;
    work_cv_.notify_one();
    return true;
  }

  int Queued() const { return queued_.load(std::memory_order_relaxed); }
  int Running() const { return running_.load(std::memory_order_relaxed); }

 private:
  void WorkerLoop(const Factory& factory) {
    std::unique_ptr<Engine> engine = factory();
    for (;;) {
      Work work;
      {
        std::unique_lock<std::mutex> lock(mutex_);
        work_cv_.wait(lock, [this] { return stop_ || !queue_.empty(); });
        if (stop_) {
          return;
        }
        work = std::move(queue_.front());
        queue_.pop_front();
        --queued_;
        ++running_;
      }
      space_cv_.notify_one();
      work(*engine);
      --running_;
    }
  }

  std::mutex mutex_;
  std::condition_variable work_cv_;
  std::condition_variable space_cv_;
  std::deque<Work> queue_;
  const std::size_t capacity_;
  bool stop_ = false;
  std::vector<std::unique_ptr<MdWorkerThread>> threads_;
  std::atomic<int> queued_{0};
  std::atomic<int> running_{0};
};

struct MdPipelineRuntime {
  explicit MdPipelineRuntime(const MdPipelineOptions& options)
      : metrics(options.font_metrics
                    ? options.font_metrics
                    : std::make_shared<MdDefaultFontMetrics>()),
        highlight(options.highlight_workers, options.queue_capacity,
                  options.worker_stack_bytes,
                  [] { return std::make_unique<MdTreeSitterParserCache>(); }),
        formula(options.formula_workers, options.queue_capacity,
                options.worker_stack_bytes,
                [] { return std::make_unique<MdFormulaEngine>(); }),
        mermaid(options.mermaid_workers, options.queue_capacity,
                options.worker_stack_bytes, [metrics = this->metrics] {
                  return std::make_unique<MdMermaidEngine>(metrics);
                }) {}

  std::shared_ptr<const MdFontMetrics> metrics;
  MdTaskLane<MdTreeSitterParserCache> highlight;
  MdTaskLane<MdFormulaEngine> formula;
  MdTaskLane<MdMermaidEngine> mermaid;
  std::atomic<int> active_jobs{0};
};

enum class MdLaneTaskKind { Highlight, Formula, Mermaid };

struct MdLaneTask {
  MdLaneTaskKind kind = MdLaneTaskKind::Highlight;
  std::string key;
  std::string payload;
  std::string language;
  bool display = false;
};

std::string AssetImgTag(MdAssetKind kind, const std::string& key,
                        const MdSvg& svg, bool display) {
  std::string tag = "<img";
  if (kind == MdAssetKind::Formula && display) {
    tag += " class=\"math-display\"";
  } else if (kind == MdAssetKind::Mermaid) {
    tag += " class=\"mermaid\"";
  }
  tag += " src=\"qlitemd-asset:";
  tag += MdAssetId(kind, key);
  tag += "\" width=\"";
  tag += std::to_string(std::max(1, svg.width));
  tag += "\" height=\"";
  tag += std::to_string(std::max(1, svg.height));
  tag += "\">";
  return tag;
}

class MdRenderJobImpl : public MdRenderJob,
                        public std::enable_shared_from_this<MdRenderJobImpl> {
 public:
  MdRenderJobImpl(std::shared_ptr<MdPipelineRuntime> runtime,
                  std::string markdown, MdRenderOptions options,
                  MdProgressCallback progress)
      : runtime_(std::move(runtime)),
        markdown_(std::move(markdown)),
        options_(options),
        progress_(std::move(progress)) {}

  ~MdRenderJobImpl() override {
    Cancel();
    if (coordinator_.joinable()) {
      coordinator_.join();
    }
  }

  void Start() {
    auto self = shared_from_this();
    coordinator_ = std::thread([self] { self->Run(); });
  }

  void Cancel() override {
    cancel_.store(true, std::memory_order_relaxed);
    state_cv_.notify_all();
  }

  bool IsFinished() const override {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return finished_;
  }

  MdHtmlResult Wait() override {
    std::lock_guard<std::mutex> join_lock(join_mutex_);
    if (coordinator_.joinable()) {
      coordinator_.join();
    }
    std::lock_guard<std::mutex> state_lock(state_mutex_);
    return result_;
  }

 private:
  void Run() {
    ++runtime_->active_jobs;
    const auto parse_start = std::chrono::steady_clock::now();
    MdParseResult parsed = ParseMarkdown(markdown_, options_.formula);
    result_.stats.source_chars = static_cast<int>(markdown_.size());
    result_.stats.parse_ms = ElapsedMs(parse_start);

    if (parsed.parse_error) {
      result_.ok = false;
      result_.error = "markdown parse failed";
      Finish();
      return;
    }

    const auto collect_start = std::chrono::steady_clock::now();
    tasks_ = CollectTasks(parsed);
    result_.stats.collect_ms = ElapsedMs(collect_start);
    result_.stats.worker_tasks = static_cast<int>(tasks_.size());
    for (const MdLaneTask& task : tasks_) {
      if (task.kind == MdLaneTaskKind::Formula) {
        ++result_.stats.formulas;
      } else if (task.kind == MdLaneTaskKind::Mermaid) {
        ++result_.stats.mermaid_blocks;
      }
    }
    {
      std::lock_guard<std::mutex> lock(state_mutex_);
      total_ = static_cast<int>(tasks_.size());
    }

    const auto worker_start = std::chrono::steady_clock::now();
    SubmitTasks();
    WaitForTasks();
    result_.stats.worker_ms = ElapsedMs(worker_start);
    result_.stats.worker_failures = failures_.load(std::memory_order_relaxed);

    if (cancel_.load(std::memory_order_relaxed)) {
      result_.ok = false;
      result_.cancelled = true;
      result_.error = "cancelled";
      Finish();
      return;
    }

    const auto assemble_start = std::chrono::steady_clock::now();
    Assemble(parsed);
    result_.stats.assemble_ms = ElapsedMs(assemble_start);
    Finish();
  }

  std::vector<MdLaneTask> CollectTasks(const MdParseResult& parsed) {
    std::vector<MdLaneTask> tasks;
    std::unordered_set<std::string> seen;
    auto add = [&tasks, &seen](MdLaneTaskKind kind, std::string key,
                               std::string payload, std::string language,
                               bool display) {
      std::string identity = std::to_string(static_cast<int>(kind)) + "|" + key;
      if (!seen.insert(identity).second) {
        return;
      }
      MdLaneTask task;
      task.kind = kind;
      task.key = std::move(key);
      task.payload = std::move(payload);
      task.language = std::move(language);
      task.display = display;
      tasks.push_back(std::move(task));
    };

    if (options_.formula) {
      for (const MdMathSegment& segment : parsed.segments) {
        const std::string tex = Trim(segment.tex);
        if (tex.empty()) {
          continue;
        }
        add(MdLaneTaskKind::Formula,
            MdFormulaRenderKey(tex, segment.display, options_.formula_color),
            tex, std::string(), segment.display);
      }
    }
    for (const MdCodeBlock& block : parsed.blocks) {
      if (block.code.empty()) {
        continue;
      }
      // Mermaid fences still enqueue a highlight task: if the render fails
      // the assembler falls back to the highlighted source block.
      add(MdLaneTaskKind::Highlight,
          MdHighlightRenderKey(block.code, block.language), block.code,
          block.language, false);
      if (options_.mermaid && block.language == "mermaid") {
        add(MdLaneTaskKind::Mermaid,
            MdMermaidRenderKey(block.code, options_.theme, *runtime_->metrics),
            block.code, "mermaid", false);
      }
    }
    return tasks;
  }

  void SubmitTasks() {
    for (std::size_t i = 0; i < tasks_.size(); ++i) {
      if (cancel_.load(std::memory_order_relaxed)) {
        TaskDone();
        continue;
      }
      auto self = shared_from_this();
      bool submitted = false;
      switch (tasks_[i].kind) {
        case MdLaneTaskKind::Highlight:
          submitted = runtime_->highlight.Submit(
              [self, i](MdTreeSitterParserCache& cache) {
                self->RunHighlight(i, cache);
              });
          break;
        case MdLaneTaskKind::Formula:
          submitted =
              runtime_->formula.Submit([self, i](MdFormulaEngine& engine) {
                self->RunFormula(i, engine);
              });
          break;
        case MdLaneTaskKind::Mermaid:
          submitted =
              runtime_->mermaid.Submit([self, i](MdMermaidEngine& engine) {
                self->RunMermaid(i, engine);
              });
          break;
      }
      if (!submitted) {
        TaskDone();
      }
    }
  }

  void WaitForTasks() {
    if (total_ == 0) {
      return;
    }
    if (!progress_) {
      std::unique_lock<std::mutex> lock(state_mutex_);
      state_cv_.wait(lock, [this] {
        return completed_ >= total_ || cancel_.load(std::memory_order_relaxed);
      });
      return;
    }
    int last = -1;
    while (last != total_) {
      std::unique_lock<std::mutex> lock(state_mutex_);
      state_cv_.wait(lock, [this, last] {
        return completed_ != last || cancel_.load(std::memory_order_relaxed);
      });
      if (cancel_.load(std::memory_order_relaxed)) {
        return;
      }
      const int completed = completed_;
      lock.unlock();
      progress_(completed, total_, total_ - completed);
      last = completed;
    }
  }

  void RunHighlight(std::size_t index, MdTreeSitterParserCache& cache) {
    const MdLaneTask& task = tasks_[index];
    std::string html;
    bool failed = false;
    try {
      html = MdHighlightCode(task.payload, task.language, &cache);
    } catch (...) {
      failed = true;
    }
    if (failed) {
      ++failures_;
    }
    if (!cancel_.load(std::memory_order_relaxed)) {
      std::lock_guard<std::mutex> lock(results_mutex_);
      highlight_results_[task.key] = std::move(html);
    }
    TaskDone();
  }

  void RunFormula(std::size_t index, MdFormulaEngine& engine) {
    const MdLaneTask& task = tasks_[index];
    engine.SetInterruptFlag(&cancel_);
    MdFormulaResult rendered;
    try {
      rendered =
          engine.Render(task.payload, task.display, options_.formula_color);
    } catch (...) {
      rendered.ok = false;
    }
    if (!rendered.ok) {
      ++failures_;
    } else if (!cancel_.load(std::memory_order_relaxed)) {
      std::lock_guard<std::mutex> lock(results_mutex_);
      formula_results_[task.key] = rendered.svg;
    }
    TaskDone();
  }

  void RunMermaid(std::size_t index, MdMermaidEngine& engine) {
    const MdLaneTask& task = tasks_[index];
    engine.SetInterruptFlag(&cancel_);
    MdMermaidResult rendered;
    try {
      rendered = engine.Render(task.payload, options_.theme);
    } catch (...) {
      rendered.ok = false;
    }
    if (!rendered.ok) {
      ++failures_;
    } else if (!cancel_.load(std::memory_order_relaxed)) {
      std::lock_guard<std::mutex> lock(results_mutex_);
      mermaid_results_[task.key] = rendered.svg;
    }
    TaskDone();
  }

  void TaskDone() {
    {
      std::lock_guard<std::mutex> lock(state_mutex_);
      ++completed_;
    }
    state_cv_.notify_all();
  }

  std::string Resolve(MdRenderKind kind, std::string_view payload,
                      std::string_view language, bool display) {
    if (kind == MdRenderKind::Highlight) {
      const std::string key = MdHighlightRenderKey(payload, language);
      std::lock_guard<std::mutex> lock(results_mutex_);
      const auto it = highlight_results_.find(key);
      return it != highlight_results_.end() ? it->second : std::string();
    }
    if (kind == MdRenderKind::Formula) {
      const std::string key =
          MdFormulaRenderKey(payload, display, options_.formula_color);
      std::lock_guard<std::mutex> lock(results_mutex_);
      const auto it = formula_results_.find(key);
      if (it == formula_results_.end()) {
        return {};
      }
      return AssetImgTag(MdAssetKind::Formula, key, it->second, display);
    }
    const std::string key =
        MdMermaidRenderKey(payload, options_.theme, *runtime_->metrics);
    std::lock_guard<std::mutex> lock(results_mutex_);
    const auto it = mermaid_results_.find(key);
    if (it == mermaid_results_.end()) {
      return {};
    }
    return AssetImgTag(MdAssetKind::Mermaid, key, it->second, false);
  }

  void Assemble(const MdParseResult& parsed) {
    MdAssembleOptions assemble_options;
    assemble_options.code_line_numbers = options_.code_line_numbers;
    result_.outline.clear();
    assemble_options.outline = [this](const MdHeading& heading) {
      result_.outline.push_back(heading);
    };

    const auto resolver = [this](MdRenderKind kind, std::string_view payload,
                                 std::string_view language, bool display) {
      return Resolve(kind, payload, language, display);
    };
    const auto code_sink = [this](std::string_view id, std::string_view code) {
      (void)id;
      MdCodeBlock block;
      block.index = static_cast<int>(result_.code_blocks.size());
      block.code = std::string(code);
      result_.code_blocks.push_back(std::move(block));
    };

    result_.html =
        AssembleMarkdown(parsed, resolver, code_sink, {}, assemble_options);
    if (result_.html.empty()) {
      result_.html = parsed.html;
    }
    result_.ok = true;
    result_.stats.html_chars = static_cast<int>(result_.html.size());
    result_.stats.headings = static_cast<int>(result_.outline.size());
    result_.stats.code_blocks = static_cast<int>(result_.code_blocks.size());

    // Assets follow task order (first occurrence wins), which is
    // deterministic for a given document.
    for (const MdLaneTask& task : tasks_) {
      MdAssetKind kind;
      const MdSvg* svg = nullptr;
      if (task.kind == MdLaneTaskKind::Formula) {
        const auto it = formula_results_.find(task.key);
        if (it == formula_results_.end()) {
          continue;
        }
        kind = MdAssetKind::Formula;
        svg = &it->second;
      } else if (task.kind == MdLaneTaskKind::Mermaid) {
        const auto it = mermaid_results_.find(task.key);
        if (it == mermaid_results_.end()) {
          continue;
        }
        kind = MdAssetKind::Mermaid;
        svg = &it->second;
      } else {
        continue;
      }
      MdAsset asset;
      asset.id = MdAssetId(kind, task.key);
      asset.kind = kind;
      asset.source = task.payload;
      asset.image = *svg;
      asset.display = task.display;
      result_.assets.push_back(std::move(asset));
    }
    result_.stats.asset_count = static_cast<int>(result_.assets.size());
  }

  void Finish() {
    {
      std::lock_guard<std::mutex> lock(state_mutex_);
      finished_ = true;
    }
    --runtime_->active_jobs;
    state_cv_.notify_all();
  }

  std::shared_ptr<MdPipelineRuntime> runtime_;
  std::string markdown_;
  MdRenderOptions options_;
  MdProgressCallback progress_;
  std::vector<MdLaneTask> tasks_;

  mutable std::mutex results_mutex_;
  std::unordered_map<std::string, std::string> highlight_results_;
  std::unordered_map<std::string, MdSvg> formula_results_;
  std::unordered_map<std::string, MdSvg> mermaid_results_;

  mutable std::mutex state_mutex_;
  std::condition_variable state_cv_;
  int total_ = 0;
  int completed_ = 0;
  bool finished_ = false;
  std::atomic<bool> cancel_{false};
  std::atomic<int> failures_{0};

  std::mutex join_mutex_;
  std::thread coordinator_;
  MdHtmlResult result_;
};

}  // namespace

struct MdHtmlPipeline::Impl {
  explicit Impl(const MdPipelineOptions& options)
      : runtime(std::make_shared<MdPipelineRuntime>(options)) {}

  ~Impl() {
    std::vector<std::shared_ptr<MdRenderJobImpl>> jobs = SnapshotJobs();
    for (const std::shared_ptr<MdRenderJobImpl>& job : jobs) {
      job->Cancel();
    }
    for (const std::shared_ptr<MdRenderJobImpl>& job : jobs) {
      job->Wait();
    }
  }

  std::vector<std::shared_ptr<MdRenderJobImpl>> SnapshotJobs() {
    std::lock_guard<std::mutex> lock(jobs_mutex);
    std::vector<std::shared_ptr<MdRenderJobImpl>> active;
    for (const std::weak_ptr<MdRenderJobImpl>& weak : jobs_) {
      if (std::shared_ptr<MdRenderJobImpl> job = weak.lock()) {
        active.push_back(std::move(job));
      }
    }
    return active;
  }

  std::shared_ptr<MdPipelineRuntime> runtime;
  std::mutex jobs_mutex;
  std::vector<std::weak_ptr<MdRenderJobImpl>> jobs_;
};

MdHtmlPipeline::MdHtmlPipeline(const MdPipelineOptions& options)
    : impl_(new Impl(options)) {}

MdHtmlPipeline::~MdHtmlPipeline() = default;

std::shared_ptr<MdRenderJob> MdHtmlPipeline::RenderAsync(
    std::string markdown, MdRenderOptions options,
    MdProgressCallback progress) {
  auto job = std::make_shared<MdRenderJobImpl>(
      impl_->runtime, std::move(markdown), options, std::move(progress));
  {
    std::lock_guard<std::mutex> lock(impl_->jobs_mutex);
    impl_->jobs_.erase(
        std::remove_if(impl_->jobs_.begin(), impl_->jobs_.end(),
                       [](const std::weak_ptr<MdRenderJobImpl>& weak) {
                         return weak.expired();
                       }),
        impl_->jobs_.end());
    impl_->jobs_.push_back(job);
  }
  job->Start();
  return job;
}

MdHtmlResult MdHtmlPipeline::Render(std::string markdown,
                                    MdRenderOptions options) {
  return RenderAsync(std::move(markdown), options)->Wait();
}

void MdHtmlPipeline::CancelAll() {
  for (const std::shared_ptr<MdRenderJobImpl>& job : impl_->SnapshotJobs()) {
    job->Cancel();
  }
}

MdServiceStatus MdHtmlPipeline::Status() const {
  MdServiceStatus status;
  status.active_jobs = impl_->runtime->active_jobs.load();
  status.queued_tasks = impl_->runtime->highlight.Queued() +
                        impl_->runtime->formula.Queued() +
                        impl_->runtime->mermaid.Queued();
  status.running_tasks = impl_->runtime->highlight.Running() +
                         impl_->runtime->formula.Running() +
                         impl_->runtime->mermaid.Running();
  return status;
}
