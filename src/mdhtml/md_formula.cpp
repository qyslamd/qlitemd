#include "md_formula.h"

#include <string>
#include <utility>

#include "md_svg.h"
#include "md_text.h"

#if defined(QLITEMD_MDHTML_FORMULA_RATEX)
#include "ratex_ffi.h"
#endif

struct MdFormulaEngine::Impl {};

MdFormulaEngine::MdFormulaEngine() : impl_(new Impl) {}

MdFormulaEngine::~MdFormulaEngine() = default;

MdFormulaResult MdFormulaEngine::Render(std::string_view tex, bool display,
                                        MdColor color) {
  MdFormulaResult result;
  const std::string normalized = Trim(tex);
  if (normalized.empty()) {
    result.error = "empty formula";
    return result;
  }

#if defined(QLITEMD_MDHTML_FORMULA_RATEX)
  RatexColor rgba{static_cast<float>(color.r) / 255.0f,
                  static_cast<float>(color.g) / 255.0f,
                  static_cast<float>(color.b) / 255.0f,
                  static_cast<float>(color.a) / 255.0f};
  // Aligns with the previous MathJax pipeline: em=16px, no padding, and the
  // same path-stroke ratio.
  RatexOptions options{};
  options.struct_size = sizeof(RatexOptions);
  options.display_mode = display ? 1 : 0;
  options.color = &rgba;
  options.font_size = 16.0;
  options.padding = 0.0;
  options.stroke_width = 0.6;

  const RatexResult rendered = ratex_render_svg(normalized.c_str(), &options);
  if (rendered.error_code != 0 || rendered.data == nullptr) {
    const char* error = ratex_get_last_error();
    result.error = error != nullptr ? error : "ratex render failed";
    ratex_free_display_list(rendered.data);
    return result;
  }
  const std::string raw(rendered.data);
  ratex_free_display_list(rendered.data);

  const std::string root = MdExtractSvgRoot(raw);
  if (root.empty()) {
    result.error = "formula svg: no root element";
    return result;
  }
  if (!MdSvgIntrinsicSize(root, &result.svg.width, &result.svg.height)) {
    result.error = "formula svg: no intrinsic size";
    return result;
  }
  result.svg.svg = std::move(root);
  result.ok = true;
  return result;
#else
  (void)display;
  (void)color;
  result.error = "formula backend unavailable in this build";
  return result;
#endif
}

std::string MdFormulaRenderKey(std::string_view tex, bool display,
                               MdColor color) {
  std::string key = display ? "D" : "I";
  key += MdColorHex(color);
  key.push_back('|');
  key += Trim(tex);
  return key;
}
