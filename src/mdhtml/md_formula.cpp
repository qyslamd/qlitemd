#include "md_formula.h"

#include <cmath>
#include <cstdlib>
#include <string>
#include <string_view>
#include <utility>

#include "md_svg.h"
#include "md_text.h"

#if defined(QLITEMD_MDHTML_FORMULA_RATEX)
#include "ratex_ffi.h"
#elif defined(QLITEMD_MDHTML_FORMULA_QUICKJS)
#include <quickjs.h>

#include "md_bytecode.h"
#include "md_js_runtime.h"
#endif

namespace {

#if defined(QLITEMD_MDHTML_FORMULA_QUICKJS)

// MathJax emits width/height in ex; the bundled payload uses em=16px, ex=8px.
bool ParseExAttribute(std::string_view svg, std::string_view name,
                      double* out) {
  const std::string needle = "\"" + std::string(name) + "=\"";
  std::size_t pos = svg.find(needle);
  if (pos == std::string_view::npos) {
    return false;
  }
  pos += needle.size();
  const std::size_t end = svg.find('"', pos);
  if (end == std::string_view::npos) {
    return false;
  }
  std::string value(svg.substr(pos, end - pos));
  while (!value.empty() && (value.back() == ' ' || value.back() == '\t')) {
    value.pop_back();
  }
  if (value.size() < 3 || value.substr(value.size() - 2) != "ex") {
    return false;
  }
  value.resize(value.size() - 2);
  char* parse_end = nullptr;
  const double number = std::strtod(value.c_str(), &parse_end);
  if (parse_end == value.c_str() || !std::isfinite(number) || number <= 0.0) {
    return false;
  }
  *out = number * 8.0;
  return true;
}

std::string EscapeJsSingleQuoted(std::string_view text) {
  std::string escaped;
  escaped.reserve(text.size() + 8);
  for (char c : text) {
    switch (c) {
      case '\\':
        escaped += "\\\\";
        break;
      case '\'':
        escaped += "\\'";
        break;
      case '\n':
        escaped += "\\n";
        break;
      case '\r':
        escaped += "\\r";
        break;
      default:
        escaped.push_back(c);
        break;
    }
  }
  return escaped;
}

#endif  // QLITEMD_MDHTML_FORMULA_QUICKJS

}  // namespace

struct MdFormulaEngine::Impl {
#if defined(QLITEMD_MDHTML_FORMULA_QUICKJS)
  MdJsRuntime runtime;
#endif

  void SetInterruptFlag(std::atomic<bool>* flag) {
#if defined(QLITEMD_MDHTML_FORMULA_QUICKJS)
    runtime.SetInterruptFlag(flag);
#else
    (void)flag;
#endif
  }
};

MdFormulaEngine::MdFormulaEngine() : impl_(new Impl) {}

MdFormulaEngine::~MdFormulaEngine() = default;

void MdFormulaEngine::SetInterruptFlag(std::atomic<bool>* flag) {
  impl_->SetInterruptFlag(flag);
}

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
#elif defined(QLITEMD_MDHTML_FORMULA_QUICKJS)
  if (impl_->runtime.status().permanently_failed) {
    result.error = "formula engine failed to load";
    return result;
  }
  if (!impl_->runtime.EnsureLoaded({{md_mj, md_mj_size, "mathjax-svg.min.js"}},
                                   nullptr, nullptr)) {
    result.error = "formula engine failed to load";
    return result;
  }

  std::string call = "try { 'SVGOK:' + MathJaxAdapter.convert('";
  call += EscapeJsSingleQuoted(normalized);
  call += display ? "', true)" : "', false)";
  call +=
      " } catch (e) { 'SVGERR: ' + "
      "(e && e.message ? e.message : e) }";
  const JSValue value = JS_Eval(impl_->runtime.context(), call.data(),
                                call.size(), "convert", JS_EVAL_TYPE_GLOBAL);
  if (JS_IsException(value)) {
    result.error = MdJsRuntime::TakeExceptionMessage(impl_->runtime.context());
    JS_FreeValue(impl_->runtime.context(), value);
    return result;
  }
  const char* raw = JS_ToCString(impl_->runtime.context(), value);
  if (raw == nullptr) {
    JS_FreeValue(impl_->runtime.context(), value);
    result.error = "formula convert: no result string";
    return result;
  }
  const std::string converted(raw);
  JS_FreeCString(impl_->runtime.context(), raw);
  JS_FreeValue(impl_->runtime.context(), value);
  if (converted.rfind("SVGOK:", 0) != 0) {
    result.error = converted;
    return result;
  }

  const std::string root = MdExtractSvgRoot(converted.substr(6));
  if (root.empty()) {
    result.error = "formula svg: no root element";
    return result;
  }
  const std::string colored = MdApplySvgColor(root, color);
  double width = 0.0;
  double height = 0.0;
  if (!ParseExAttribute(colored, "width", &width) ||
      !ParseExAttribute(colored, "height", &height)) {
    int intrinsic_width = 0;
    int intrinsic_height = 0;
    if (!MdSvgIntrinsicSize(colored, &intrinsic_width, &intrinsic_height)) {
      result.error = "formula svg: no intrinsic size";
      return result;
    }
    // MathJax viewBox units are 1/1000 em.
    width = colored.find("viewBox") != std::string::npos
                ? static_cast<double>(intrinsic_width) * 16.0 / 1000.0
                : static_cast<double>(intrinsic_width);
    height = colored.find("viewBox") != std::string::npos
                 ? static_cast<double>(intrinsic_height) * 16.0 / 1000.0
                 : static_cast<double>(intrinsic_height);
  }
  result.svg.width = static_cast<int>(std::lround(width));
  result.svg.height = static_cast<int>(std::lround(height));
  if (result.svg.width < 1 || result.svg.height < 1) {
    result.error = "formula svg: bad size";
    return result;
  }
  result.svg.svg = std::move(colored);
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
