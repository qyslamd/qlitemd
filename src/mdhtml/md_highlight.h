#ifndef FILE_MDHTML_MD_HIGHLIGHT_H_
#define FILE_MDHTML_MD_HIGHLIGHT_H_

#include <atomic>
#include <memory>
#include <string>
#include <string_view>

// Qt-free syntax highlighting: highlight.js runs inside the per-worker
// QuickJS runtime (the same bytecode payload pipeline as MathJax) and returns
// HTML spans carrying the short CSS class names used by the themes
// (kw/str/num/cmt/ty/fn/var/op/attr). Spans close at every newline, unknown
// languages are plain-escaped without language guessing.
//
// One MdHighlightEngine per worker thread: QuickJS runtimes are not
// thread-safe. The engine is created lazily and loads the bundled payload on
// first use; a load failure is permanent for that engine and callers fall
// back to escaping.
class MdHighlightEngine {
 public:
  MdHighlightEngine();
  ~MdHighlightEngine();
  MdHighlightEngine(const MdHighlightEngine&) = delete;
  MdHighlightEngine& operator=(const MdHighlightEngine&) = delete;
  MdHighlightEngine(MdHighlightEngine&&) = delete;
  MdHighlightEngine& operator=(MdHighlightEngine&&) = delete;

  bool EnsureLoaded();

  // Interrupts a running highlight call; the flag is owned by the caller and
  // must outlive the engine. Pass nullptr to clear.
  void SetInterruptFlag(std::atomic<bool>* flag);

 private:
  friend std::string MdHighlightCode(std::string_view code,
                                     std::string_view language,
                                     MdHighlightEngine* engine);
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

// Highlights code. Empty input, unknown languages and payload/JSC failures
// fall back to escaped source (a highlighting failure must not hide text).
// engine may be null for one-off use; production callers pass a per-thread
// engine so the QuickJS runtime and parsed function are reused.
std::string MdHighlightCode(std::string_view code, std::string_view language,
                            MdHighlightEngine* engine);

// Stable dedupe key shared by the scheduler and render caches.
std::string MdHighlightRenderKey(std::string_view code,
                                 std::string_view language);

#endif  // FILE_MDHTML_MD_HIGHLIGHT_H_
