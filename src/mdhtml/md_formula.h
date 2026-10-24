#ifndef FILE_MDHTML_MD_FORMULA_H_
#define FILE_MDHTML_MD_FORMULA_H_

#include <memory>
#include <string>
#include <string_view>

#include "md_types.h"

struct MdFormulaResult {
  bool ok = false;
  MdSvg svg;
  std::string error;
};

// One TeX -> SVG engine per worker thread. The RaTeX backend is stateless;
// the QuickJS fallback owns a runtime and must not be shared.
class MdFormulaEngine {
 public:
  MdFormulaEngine();
  ~MdFormulaEngine();
  MdFormulaEngine(const MdFormulaEngine&) = delete;
  MdFormulaEngine& operator=(const MdFormulaEngine&) = delete;

  MdFormulaResult Render(std::string_view tex, bool display, MdColor color);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

// Stable dedupe key: trimmed TeX, display flag and colour.
std::string MdFormulaRenderKey(std::string_view tex, bool display,
                               MdColor color);

#endif  // FILE_MDHTML_MD_FORMULA_H_
