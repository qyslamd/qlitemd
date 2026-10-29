#ifndef FILE_MDHTML_MD_FORMULA_H_
#define FILE_MDHTML_MD_FORMULA_H_

#include <atomic>
#include <memory>
#include <string>
#include <string_view>

#include "md_types.h"

struct MdFormulaResult {
  bool ok = false;
  MdSvg svg;
  std::string error;
  // The backend that actually produced the SVG; meaningful only when ok.
  MdFormulaBackend backend_used = MdFormulaBackend::kAuto;
};

// Compile-time availability of each backend; a runtime request for a
// missing backend degrades to the other one.
bool MdFormulaNativeAvailable();
bool MdFormulaQuickJsAvailable();

// "auto" / "ratex" / "quickjs"; for status lines and cache keys.
const char* MdFormulaBackendName(MdFormulaBackend backend);

// One TeX -> SVG engine per worker thread. Both backends compiled into the
// binary live here side by side and are constructed lazily on first use;
// the RaTeX backend is stateless, while QuickJS owns a runtime that must not
// be shared.
class MdFormulaEngine {
 public:
  MdFormulaEngine();
  ~MdFormulaEngine();
  MdFormulaEngine(const MdFormulaEngine&) = delete;
  MdFormulaEngine& operator=(const MdFormulaEngine&) = delete;

  // kAuto tries RaTeX first and falls back to MathJax per formula when the
  // native render fails. Explicit modes degrade to the other backend when
  // the requested one is not compiled in. *backend_used reports which
  // backend actually produced the SVG.
  MdFormulaResult Render(std::string_view tex, bool display, MdColor color,
                         MdFormulaBackend mode = MdFormulaBackend::kAuto,
                         MdFormulaBackend* backend_used = nullptr);

  // QuickJS only: setting the flag interrupts a running evaluation. The
  // native RaTeX backend is not interruptible and ignores it.
  void SetInterruptFlag(std::atomic<bool>* flag);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

// Stable dedupe key: trimmed TeX, display flag, colour, compiled backends
// and the requested backend mode.
std::string MdFormulaRenderKey(std::string_view tex, bool display,
                               MdColor color,
                               MdFormulaBackend mode = MdFormulaBackend::kAuto);

#endif  // FILE_MDHTML_MD_FORMULA_H_
