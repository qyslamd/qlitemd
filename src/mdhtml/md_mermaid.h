#ifndef FILE_MDHTML_MD_MERMAID_H_
#define FILE_MDHTML_MD_MERMAID_H_

#include <memory>
#include <string>
#include <string_view>

#include "md_font_metrics.h"
#include "md_types.h"

struct MdMermaidResult {
  bool ok = false;
  MdSvg svg;
  std::string error;
};

// One Mermaid -> SVG engine per worker thread. The native backend owns two
// merman handles (light/dark) and is not shareable; the injected metrics are
// shared and must outlive the engine.
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
  MdMermaidResult Render(std::string_view code, MdTheme theme);

 private:
  std::unique_ptr<Impl> impl_;
};

// Stable dedupe key: theme, metrics fingerprint, backend version and source.
std::string MdMermaidRenderKey(std::string_view code, MdTheme theme,
                               const MdFontMetrics& metrics);

#endif  // FILE_MDHTML_MD_MERMAID_H_
