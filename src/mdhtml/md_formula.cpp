#include "md_formula.h"

#include <cmath>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "md_svg.h"
#include "md_text.h"

#if defined(QLITEMD_MDHTML_FORMULA_RATEX)
#include "ratex_ffi.h"
#endif
#if defined(QLITEMD_MDHTML_QUICKJS)
#include <quickjs.h>

#include "md_bytecode.h"
#include "md_js_runtime.h"
#endif

namespace {

#if defined(QLITEMD_MDHTML_QUICKJS)

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

// QuickJS + MathJax backend. The runtime is lazy so an explicit native
// request never pays for loading the payload.
class MdFormulaJs {
 public:
  MdFormulaResult Render(std::string_view normalized, bool display,
                         MdColor color) {
    MdFormulaResult result;
    if (runtime_.status().permanently_failed) {
      result.error = "formula engine failed to load";
      return result;
    }
    if (!runtime_.EnsureLoaded({{md_mj, md_mj_size, "mathjax-svg.min.js"}},
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
    const JSValue value = JS_Eval(runtime_.context(), call.data(), call.size(),
                                  "convert", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(value)) {
      result.error = MdJsRuntime::TakeExceptionMessage(runtime_.context());
      JS_FreeValue(runtime_.context(), value);
      return result;
    }
    const char* raw = JS_ToCString(runtime_.context(), value);
    if (raw == nullptr) {
      JS_FreeValue(runtime_.context(), value);
      result.error = "formula convert: no result string";
      return result;
    }
    const std::string converted(raw);
    JS_FreeCString(runtime_.context(), raw);
    JS_FreeValue(runtime_.context(), value);
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
  }

  void SetInterruptFlag(std::atomic<bool>* flag) {
    runtime_.SetInterruptFlag(flag);
  }

 private:
  MdJsRuntime runtime_;
};

#endif  // QLITEMD_MDHTML_QUICKJS

#if defined(QLITEMD_MDHTML_FORMULA_RATEX)

MdFormulaResult RenderWithRatex(std::string_view normalized, bool display,
                                MdColor color) {
  MdFormulaResult result;
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

  const std::string source(normalized);
  const RatexResult rendered = ratex_render_svg(source.c_str(), &options);
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
}

#endif  // QLITEMD_MDHTML_FORMULA_RATEX

}  // namespace

struct MdFormulaEngine::Impl {
#if defined(QLITEMD_MDHTML_QUICKJS)
  std::optional<MdFormulaJs> js;
  std::atomic<bool>* interrupt_flag = nullptr;

  MdFormulaJs& Js() {
    if (!js.has_value()) {
      js.emplace();
      if (interrupt_flag != nullptr) {
        // A native attempt may have failed before this lazy fallback; the
        // flag was already set on the engine, so hand it to the new runtime.
        js->SetInterruptFlag(interrupt_flag);
      }
    }
    return *js;
  }
#endif

  void SetInterruptFlag(std::atomic<bool>* flag) {
#if defined(QLITEMD_MDHTML_QUICKJS)
    // Constructing the JS runtime just to set a flag would start the payload
    // load; remember it and apply it when the runtime is created.
    interrupt_flag = flag;
    if (js.has_value()) {
      js->SetInterruptFlag(flag);
    }
#else
    (void)flag;
#endif
  }

  bool TryNative(std::string_view normalized, bool display, MdColor color,
                 MdFormulaResult* out) {
#if defined(QLITEMD_MDHTML_FORMULA_RATEX)
    *out = RenderWithRatex(normalized, display, color);
    if (out->ok) {
      out->backend_used = MdFormulaBackend::kNative;
      return true;
    }
    out->error = "ratex: " + out->error;
    return false;
#else
    (void)normalized;
    (void)display;
    (void)color;
    (void)out;
    return false;
#endif
  }

  bool TryJs(std::string_view normalized, bool display, MdColor color,
             MdFormulaResult* out) {
#if defined(QLITEMD_MDHTML_QUICKJS)
    *out = Js().Render(normalized, display, color);
    if (out->ok) {
      out->backend_used = MdFormulaBackend::kQuickjs;
      return true;
    }
    out->error = "quickjs: " + out->error;
    return false;
#else
    (void)normalized;
    (void)display;
    (void)color;
    (void)out;
    return false;
#endif
  }

  MdFormulaResult Render(std::string_view tex, bool display, MdColor color,
                         MdFormulaBackend mode, MdFormulaBackend* used) {
    constexpr bool kHasNative =
#if defined(QLITEMD_MDHTML_FORMULA_RATEX)
        true;
#else
        false;
#endif
    constexpr bool kHasJs =
#if defined(QLITEMD_MDHTML_QUICKJS)
        true;
#else
        false;
#endif

    const std::string normalized = Trim(tex);
    MdFormulaResult out;
    if (normalized.empty()) {
      out.error = "empty formula";
      return out;
    }

    // auto tries RaTeX first, then falls back per formula to MathJax. An
    // explicit request is pinned to its backend; it only degrades to the
    // other engine when that backend was not compiled in at all.
    auto accept = [&]() {
      if (used != nullptr) {
        *used = out.backend_used;
      }
      return out;
    };

    if (mode == MdFormulaBackend::kNative) {
      if (kHasNative) {
        if (TryNative(normalized, display, color, &out)) {
          return accept();
        }
      } else if (kHasJs && TryJs(normalized, display, color, &out)) {
        return accept();
      }
      if (!out.ok && out.error.empty()) {
        out.error = "formula backend unavailable in this build";
      }
      return out;
    }
    if (mode == MdFormulaBackend::kQuickjs) {
      if (kHasJs) {
        if (TryJs(normalized, display, color, &out)) {
          return accept();
        }
      } else if (kHasNative && TryNative(normalized, display, color, &out)) {
        return accept();
      }
      if (!out.ok && out.error.empty()) {
        out.error = "formula backend unavailable in this build";
      }
      return out;
    }

    std::string native_error;
    if (kHasNative) {
      if (TryNative(normalized, display, color, &out)) {
        return accept();
      }
      native_error = out.error;
    }
    if (kHasJs) {
      if (TryJs(normalized, display, color, &out)) {
        return accept();
      }
      if (!native_error.empty()) {
        out.error = native_error + "; " + out.error;
      }
      return out;
    }
    out.error = native_error.empty()
                    ? "formula backend unavailable in this build"
                    : native_error;
    return out;
  }
};

MdFormulaEngine::MdFormulaEngine() : impl_(new Impl) {}

MdFormulaEngine::~MdFormulaEngine() = default;

void MdFormulaEngine::SetInterruptFlag(std::atomic<bool>* flag) {
  impl_->SetInterruptFlag(flag);
}

MdFormulaResult MdFormulaEngine::Render(std::string_view tex, bool display,
                                        MdColor color, MdFormulaBackend mode,
                                        MdFormulaBackend* backend_used) {
  return impl_->Render(tex, display, color, mode, backend_used);
}

bool MdFormulaNativeAvailable() {
#if defined(QLITEMD_MDHTML_FORMULA_RATEX)
  return true;
#else
  return false;
#endif
}

bool MdFormulaQuickJsAvailable() {
#if defined(QLITEMD_MDHTML_QUICKJS)
  return true;
#else
  return false;
#endif
}

const char* MdFormulaBackendName(MdFormulaBackend backend) {
  switch (backend) {
    case MdFormulaBackend::kNative:
      return "ratex";
    case MdFormulaBackend::kQuickjs:
      return "quickjs";
    case MdFormulaBackend::kAuto:
      break;
  }
  return "auto";
}

std::string MdFormulaRenderKey(std::string_view tex, bool display,
                               MdColor color, MdFormulaBackend mode) {
  std::string key = display ? "D" : "I";
  key += MdColorHex(color);
  std::string compiled;
  if (MdFormulaNativeAvailable()) {
    // Keep in sync with the RaTeX VERSION.md; this busts persistent caches
    // when the native output changes.
    compiled = "ratex-0.1.15";
  }
  if (MdFormulaQuickJsAvailable()) {
    compiled += compiled.empty() ? "" : "+";
    compiled += "quickjs";
  }
  if (compiled.empty()) {
    compiled = "none";
  }
  key += "|fm:";
  key += compiled;
  key.push_back(':');
  key += MdFormulaBackendName(mode);
  key.push_back('|');
  key += Trim(tex);
  return key;
}
