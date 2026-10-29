#ifndef FILE_MDHTML_MD_MERMAID_H_
#define FILE_MDHTML_MD_MERMAID_H_

#include <atomic>
#include <memory>
#include <string>
#include <string_view>

#include "md_font_metrics.h"
#include "md_types.h"

struct MdMermaidResult {
  bool ok = false;
  MdSvg svg;
  std::string error;
  // The backend that actually produced the SVG; meaningful only when ok.
  MdMermaidBackend backend_used = MdMermaidBackend::kAuto;
};

// Compile-time availability of each backend; a runtime request for a
// missing backend degrades to the other one.
bool MdMermaidNativeAvailable();
bool MdMermaidQuickJsAvailable();

// "auto" / "merman" / "quickjs"; for status lines and cache keys.
const char* MdMermaidBackendName(MdMermaidBackend backend);

// One Mermaid -> SVG engine per worker thread. Both backends compiled into
// the binary live here side by side and are constructed lazily on first
// use; the injected metrics are shared and must outlive the engine.
class MdMermaidEngine {
 public:
  // Opaque state, public only because the C measurement callback receives
  // it as user data.
  struct Impl;

  explicit MdMermaidEngine(std::shared_ptr<const MdFontMetrics> metrics);
  ~MdMermaidEngine();
  MdMermaidEngine(const MdMermaidEngine&) = delete;
  MdMermaidEngine& operator=(const MdMermaidEngine&) = delete;

  bool EnsureLoaded();
  // kAuto tries merman first and falls back to mermaid.js per diagram when
  // merman fails. Explicit modes degrade to the other backend when the
  // requested one is not compiled in. *backend_used reports which backend
  // actually produced the SVG.
  MdMermaidResult Render(std::string_view code, MdTheme theme,
                         MdMermaidBackend mode = MdMermaidBackend::kAuto,
                         MdMermaidBackend* backend_used = nullptr);

  // QuickJS only: interrupts the running render. The native merman backend
  // currently finishes the current diagram.
  void SetInterruptFlag(std::atomic<bool>* flag);

 private:
  std::unique_ptr<Impl> impl_;
};

// Stable dedupe key: theme, metrics fingerprint, compiled backends, the
// requested backend mode and source.
std::string MdMermaidRenderKey(std::string_view code, MdTheme theme,
                               const MdFontMetrics& metrics,
                               MdMermaidBackend mode = MdMermaidBackend::kAuto);

#endif  // FILE_MDHTML_MD_MERMAID_H_
