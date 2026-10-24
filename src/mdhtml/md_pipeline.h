#ifndef FILE_MDHTML_MD_PIPELINE_H_
#define FILE_MDHTML_MD_PIPELINE_H_

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "md_font_metrics.h"
#include "md_types.h"

struct MdRenderOptions {
  MdTheme theme = MdTheme::Light;
  MdColor formula_color{};
  bool formula = true;
  bool mermaid = true;
  bool code_line_numbers = false;
  MdSvgProfile svg_profile = MdSvgProfile::kPortable;
};

struct MdRenderStats {
  int source_chars = 0;
  int html_chars = 0;
  int headings = 0;
  int code_blocks = 0;
  int formulas = 0;
  int mermaid_blocks = 0;
  int asset_count = 0;
  int worker_tasks = 0;
  int worker_failures = 0;
  int64_t parse_ms = 0;
  int64_t collect_ms = 0;
  int64_t worker_ms = 0;
  int64_t assemble_ms = 0;
};

struct MdHtmlResult {
  bool ok = false;
  bool cancelled = false;
  std::string error;
  std::string html;
  std::vector<MdAsset> assets;
  std::vector<MdHeading> outline;
  std::vector<MdCodeBlock> code_blocks;
  MdRenderStats stats;
};

class MdRenderJob {
 public:
  virtual ~MdRenderJob() = default;
  virtual void Cancel() = 0;
  virtual bool IsFinished() const = 0;
  // Blocks until the coordinator finishes; safe to call more than once.
  virtual MdHtmlResult Wait() = 0;
};

using MdProgressCallback =
    std::function<void(int completed, int total, int in_flight)>;

struct MdPipelineOptions {
  // Text metrics are a pipeline property: each Mermaid worker owns a native
  // engine configured for these metrics. Null selects MdDefaultFontMetrics.
  std::shared_ptr<const MdFontMetrics> font_metrics;
  std::size_t highlight_workers = 2;
  std::size_t formula_workers = 2;
  std::size_t mermaid_workers = 4;
  // Bounded per-lane queue; a full lane applies backpressure to the
  // coordinator instead of growing without bound.
  std::size_t queue_capacity = 256;
  // Windows QuickJS needs a wide native stack; POSIX keeps the default.
  std::size_t worker_stack_bytes = 32u * 1024u * 1024u;
};

struct MdServiceStatus {
  int active_jobs = 0;
  int queued_tasks = 0;
  int running_tasks = 0;
};

// Parse and assemble run on a per-job coordinator thread; highlight,
// formula and Mermaid run on three long-lived worker lanes. Only one job's
// parse/assembly is single-threaded: assembly looks results up by key, so
// worker completion order never affects the HTML.
class MdHtmlPipeline {
 public:
  explicit MdHtmlPipeline(const MdPipelineOptions& options = {});
  ~MdHtmlPipeline();
  MdHtmlPipeline(const MdHtmlPipeline&) = delete;
  MdHtmlPipeline& operator=(const MdHtmlPipeline&) = delete;

  std::shared_ptr<MdRenderJob> RenderAsync(std::string markdown,
                                           MdRenderOptions options,
                                           MdProgressCallback progress = {});
  // Convenience synchronous entry: still uses the worker lanes.
  MdHtmlResult Render(std::string markdown, MdRenderOptions options = {});

  void CancelAll();
  MdServiceStatus Status() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

#endif  // FILE_MDHTML_MD_PIPELINE_H_
