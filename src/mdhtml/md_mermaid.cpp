#include "md_mermaid.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "md_svg.h"
#include "md_text.h"

#if defined(QLITEMD_MDHTML_MERMAID_MERMAN)
#include "merman.h"
#endif
#if defined(QLITEMD_MDHTML_QUICKJS)
#include <quickjs.h>

#include "md_bytecode.h"
#include "md_js_runtime.h"
#endif

namespace {

const char* ThemeName(MdTheme theme) {
  return theme == MdTheme::Dark ? "dark" : "default";
}

int ParseWeight(std::string_view weight) {
  const std::string lower = ToLowerAscii(weight);
  if (lower == "bold" || lower == "bolder") {
    return 700;
  }
  if (lower == "lighter") {
    return 300;
  }
  int value = 0;
  bool has_digit = false;
  for (char c : lower) {
    if (std::isdigit(static_cast<unsigned char>(c)) == 0) {
      has_digit = false;
      break;
    }
    has_digit = true;
    value = value * 10 + (c - '0');
  }
  return has_digit ? value : 400;
}

struct MeasuredText {
  double width = 0.0;
  double height = 0.0;
  double ascent = 0.0;
  double descent = 0.0;
  double raw_width = 0.0;
  int line_count = 1;
};

struct WrapLayout {
  double width = 0.0;
  int lines = 0;
};

WrapLayout WrapText(std::string_view text, const MdFontSpec& font,
                    double max_width, const MdFontMetrics& metrics) {
  WrapLayout layout;
  const double space_width = metrics.Measure(" ", font).width;
  std::size_t line_start = 0;
  while (true) {
    const std::size_t newline = text.find('\n', line_start);
    const std::string_view line =
        newline == std::string_view::npos
            ? text.substr(line_start)
            : text.substr(line_start, newline - line_start);
    double current = 0.0;
    bool has_word = false;
    std::size_t pos = 0;
    while (true) {
      const std::size_t space = line.find(' ', pos);
      const std::string_view word = space == std::string_view::npos
                                        ? line.substr(pos)
                                        : line.substr(pos, space - pos);
      // A single word wider than max_width is allowed to overflow; breaking
      // inside words would need real line-breaking rules for CJK.
      const double word_width =
          word.empty() ? 0.0 : metrics.Measure(word, font).width;
      if (has_word && current + space_width + word_width > max_width) {
        layout.width = std::max(layout.width, current);
        ++layout.lines;
        current = 0.0;
        has_word = false;
      }
      if (!word.empty()) {
        if (has_word) {
          current += space_width;
        }
        current += word_width;
        has_word = true;
      }
      if (space == std::string_view::npos) {
        break;
      }
      pos = space + 1;
    }
    layout.width = std::max(layout.width, current);
    ++layout.lines;
    if (newline == std::string_view::npos) {
      break;
    }
    line_start = newline + 1;
  }
  return layout;
}

MeasuredText MeasureText(std::string_view text, std::string_view family,
                         double size_px, std::string_view weight,
                         std::string_view style, bool wrap, double max_width,
                         double line_height, const MdFontMetrics& metrics) {
  MdFontSpec font;
  font.family = metrics.ResolveFamily(family);
  font.size_px = std::max(1.0, size_px);
  font.weight = ParseWeight(weight);
  const std::string lower_style = ToLowerAscii(style);
  font.italic = lower_style == "italic" || lower_style == "oblique";

  MeasuredText measured;
  if (!text.empty()) {
    const MdTextMetrics single = metrics.Measure(text, font);
    measured.width = single.width;
    measured.raw_width = single.width;
    measured.ascent = single.ascent;
    measured.descent = single.descent;
  }
  if (wrap && max_width > 0.0) {
    const WrapLayout layout = WrapText(text, font, max_width, metrics);
    measured.width = layout.width;
    measured.line_count = std::max(1, layout.lines);
  }
  const double line =
      line_height > 0.0 ? line_height : measured.ascent + measured.descent;
  measured.height = measured.line_count * line;
  return measured;
}

MdMermaidResult FinishSvg(MdMermaidResult out, const std::string& svg) {
  const std::string root = MdExtractSvgRoot(svg);
  if (root.empty()) {
    out.error = "mermaid svg: no root element";
    return out;
  }
  if (!MdSvgIntrinsicSize(root, &out.svg.width, &out.svg.height)) {
    out.error = "mermaid svg: no intrinsic size";
    return out;
  }
  out.svg.svg = root;
  out.ok = true;
  return out;
}

#if defined(QLITEMD_MDHTML_MERMAID_MERMAN)

std::string JsonEscape(std::string_view text) {
  std::string out;
  out.reserve(text.size() + 8);
  for (char c : text) {
    switch (c) {
      case '"':
        out += "\\\"";
        break;
      case '\\':
        out += "\\\\";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\t':
        out += "\\t";
        break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) {
          constexpr char kHex[] = "0123456789abcdef";
          out += "\\u00";
          out.push_back(kHex[(c >> 4) & 0x0F]);
          out.push_back(kHex[c & 0x0F]);
        } else {
          out.push_back(c);
        }
        break;
    }
  }
  return out;
}

// Minimal extraction of a quoted JSON string field; enough for merman's
// {"message": "..."} error payload without pulling a JSON parser into the
// core.
bool JsonStringField(std::string_view json, std::string_view key,
                     std::string* out) {
  const std::string needle = "\"" + std::string(key) + "\"";
  std::size_t pos = json.find(needle);
  if (pos == std::string_view::npos) {
    return false;
  }
  pos = json.find(':', pos + needle.size());
  if (pos == std::string_view::npos) {
    return false;
  }
  ++pos;
  while (pos < json.size() &&
         std::isspace(static_cast<unsigned char>(json[pos])) != 0) {
    ++pos;
  }
  if (pos >= json.size() || json[pos] != '"') {
    return false;
  }
  ++pos;
  std::string value;
  while (pos < json.size()) {
    const char c = json[pos++];
    if (c == '"') {
      *out = std::move(value);
      return true;
    }
    if (c == '\\' && pos < json.size()) {
      const char escaped = json[pos++];
      switch (escaped) {
        case 'n':
          value.push_back('\n');
          break;
        case 'r':
          value.push_back('\r');
          break;
        case 't':
          value.push_back('\t');
          break;
        case 'u':
          // Error messages from the backend are ASCII; keep the escape
          // visible rather than pretending to decode it.
          value += "\\u";
          break;
        default:
          value.push_back(escaped);
          break;
      }
      continue;
    }
    value.push_back(c);
  }
  return false;
}

std::string EngineOptions(MdTheme theme, const MdFontMetrics& metrics) {
  const std::string family = metrics.ResolveFamily("sans-serif");
  std::string json = "{\"site_config\":{\"theme\":\"";
  json += ThemeName(theme);
  json += "\",\"look\":\"classic\"";
  if (!family.empty()) {
    const std::string escaped = JsonEscape(family);
    json += ",\"fontFamily\":\"";
    json += escaped;
    json += "\",\"themeVariables\":{\"fontFamily\":\"";
    json += escaped;
    json += "\"}";
  }
  json += "}}";
  return json;
}

std::string SliceToString(const MermanNativeSlice& slice) {
  if (slice.data == nullptr || slice.len == 0) {
    return {};
  }
  return std::string(reinterpret_cast<const char*>(slice.data), slice.len);
}

std::string BufferToString(const MermanNativeBuffer& buffer) {
  if (buffer.data == nullptr || buffer.len == 0) {
    return {};
  }
  return std::string(reinterpret_cast<const char*>(buffer.data), buffer.len);
}

MermanNativeSlice BorrowSlice(const std::string& bytes) {
  MermanNativeSlice slice;
  slice.struct_size = MERMAN_NATIVE_STRUCT_SIZE(MermanNativeSlice);
  slice.data = reinterpret_cast<const uint8_t*>(bytes.data());
  slice.len = bytes.size();
  return slice;
}

std::string StatusName(MermanNativeStatus status) {
  switch (status) {
    case MERMAN_NATIVE_STATUS_OK:
      return "ok";
    case MERMAN_NATIVE_STATUS_INVALID_ARGUMENT:
      return "invalid-argument";
    case MERMAN_NATIVE_STATUS_OPTIONS_JSON_ERROR:
      return "options-json-error";
    case MERMAN_NATIVE_STATUS_NO_DIAGRAM:
      return "no-diagram";
    case MERMAN_NATIVE_STATUS_PARSE_ERROR:
      return "parse-error";
    case MERMAN_NATIVE_STATUS_RENDER_ERROR:
      return "render-error";
    case MERMAN_NATIVE_STATUS_UNSUPPORTED_OPERATION:
      return "unsupported-operation";
    case MERMAN_NATIVE_STATUS_BUSY:
      return "busy";
    case MERMAN_NATIVE_STATUS_CANCELLED:
      return "cancelled";
    default:
      return "status-" + std::to_string(static_cast<int>(status));
  }
}

std::string ErrorMessage(const MermanNativeResult& result) {
  const std::string json = BufferToString(result.metadata_or_error_json);
  if (!json.empty()) {
    std::string message;
    if (JsonStringField(json, "message", &message) && !message.empty()) {
      return message;
    }
    return json;
  }
  return StatusName(result.status);
}

#endif  // QLITEMD_MDHTML_MERMAID_MERMAN

}  // namespace

#if defined(QLITEMD_MDHTML_MERMAID_MERMAN)

// Native merman backend: two engine tokens (light/dark), created through the
// FFI on first use. Not shareable across threads; one instance per worker.
class MdMermaidNative {
 public:
  explicit MdMermaidNative(std::shared_ptr<const MdFontMetrics> shared)
      : metrics(std::move(shared)) {
    if (!metrics) {
      metrics = std::make_shared<MdDefaultFontMetrics>();
    }
  }

  ~MdMermaidNative() {
    if (api_ready && api.engine_try_close != nullptr) {
      for (MermanNativeEngineToken& token : engines) {
        if (token != 0) {
          api.engine_try_close(token);
          token = 0;
        }
      }
    }
  }

  MdMermaidNative(const MdMermaidNative&) = delete;
  MdMermaidNative& operator=(const MdMermaidNative&) = delete;

  bool EnsureLoaded() {
    if (loaded) {
      return true;
    }
    if (permanently_failed) {
      return false;
    }

    MermanNativeApiRequest request{};
    request.struct_size = MERMAN_NATIVE_STRUCT_SIZE(MermanNativeApiRequest);
    request.expected_abi_version = MERMAN_NATIVE_ABI_VERSION;
    const char* digest = MERMAN_NATIVE_ABI_MINIMUM_PREFIX_LAYOUT_DIGEST;
    request.expected_minimum_prefix_layout_digest.data =
        reinterpret_cast<const uint8_t*>(digest);
    request.expected_minimum_prefix_layout_digest.len = std::strlen(digest);
    request.expected_minimum_prefix_layout_digest.struct_size =
        MERMAN_NATIVE_STRUCT_SIZE(MermanNativeSlice);

    api.struct_size = MERMAN_NATIVE_STRUCT_SIZE(MermanNativeApi);
    if (merman_get_native_api(&request, &api) != MERMAN_NATIVE_STATUS_OK) {
      permanently_failed = true;
      error = "merman ABI discovery failed";
      return false;
    }
    api_ready = true;
    version = SliceToString(api.package_version);

    for (int index = 0; index < 2; ++index) {
      const std::string options =
          EngineOptions(index == 0 ? MdTheme::Light : MdTheme::Dark, *metrics);
      MermanNativeEngineConfig config{};
      config.struct_size = MERMAN_NATIVE_STRUCT_SIZE(MermanNativeEngineConfig);
      config.options_json = BorrowSlice(options);
      config.text_measure = &MeasureTextCallback;
      config.text_measure_user_data = this;
      // MermanNativeResult carries a uint64 allocation token; the x86 ABI
      // requires both the result record and out_engine to be 8-byte aligned.
      alignas(8) MermanNativeResult result = MERMAN_NATIVE_RESULT_INIT;
      const MermanNativeStatus status =
          api.engine_new(&config, &engines[index], &result);
      if (status != MERMAN_NATIVE_STATUS_OK ||
          result.status != MERMAN_NATIVE_STATUS_OK) {
        error = ErrorMessage(result);
        api.result_free(&result);
        permanently_failed = true;
        return false;
      }
      api.result_free(&result);
    }
    loaded = true;
    return true;
  }

  MdMermaidResult Render(std::string_view code, MdTheme theme) {
    MdMermaidResult out;
    if (!EnsureLoaded()) {
      out.error = error;
      return out;
    }

    static std::atomic<uint64_t> diagram_id{0};
    const std::string source(code);
    const std::string options =
        "{\"svg\":{\"diagram_id\":\"qlitemd-" +
        std::to_string(diagram_id.fetch_add(1, std::memory_order_relaxed) + 1) +
        "\",\"pipeline\":\"resvg-safe\"}}";
    MermanNativeOperationRequest request{};
    request.struct_size =
        MERMAN_NATIVE_STRUCT_SIZE(MermanNativeOperationRequest);
    request.operation = MERMAN_NATIVE_OPERATION_SVG;
    request.source = BorrowSlice(source);
    request.uri.struct_size = MERMAN_NATIVE_STRUCT_SIZE(MermanNativeSlice);
    request.options_json = BorrowSlice(options);

    const size_t engine_index = theme == MdTheme::Dark ? 1 : 0;
    alignas(8) MermanNativeResult result = MERMAN_NATIVE_RESULT_INIT;
    const MermanNativeStatus status =
        api.execute_collect(engines[engine_index], &request, &result);
    if (status != MERMAN_NATIVE_STATUS_OK ||
        result.status != MERMAN_NATIVE_STATUS_OK) {
      out.error = ErrorMessage(result);
      api.result_free(&result);
      return out;
    }
    const std::string svg = BufferToString(result.data);
    api.result_free(&result);
    if (svg.empty()) {
      out.error = "merman render: empty svg";
      return out;
    }
    return FinishSvg(std::move(out), svg);
  }

  const std::string& Version() const { return version; }

 private:
  static MermanNativeStatus MeasureTextCallback(
      const MermanNativeTextMeasureRequest* request,
      MermanNativeTextMeasureResult* result, void* user_data) noexcept {
    if (request == nullptr || result == nullptr || user_data == nullptr) {
      return MERMAN_NATIVE_STATUS_INVALID_ARGUMENT;
    }
    auto* engine = static_cast<MdMermaidNative*>(user_data);
    try {
      result->struct_size =
          MERMAN_NATIVE_STRUCT_SIZE(MermanNativeTextMeasureResult);
      result->handled = 0;
      result->has_raw_width = 0;
      result->raw_width = 0.0;
      if (!engine->metrics) {
        return MERMAN_NATIVE_STATUS_INVALID_ARGUMENT;
      }

      const bool wrapping =
          request->has_max_width != 0 && request->max_width > 0.0 &&
          request->wrap_mode != MERMAN_TEXT_WRAP_MODE_SVG_LIKE_SINGLE_RUN;
      const MeasuredText measured = MeasureText(
          SliceToString(request->text), SliceToString(request->font_family),
          request->font_size, SliceToString(request->font_weight),
          SliceToString(request->font_style), wrapping, request->max_width,
          request->line_height, *engine->metrics);
      auto set_metrics = [&]() {
        result->result_kind = MERMAN_TEXT_MEASUREMENT_RESULT_KIND_METRICS;
        result->width = measured.width;
        result->height = measured.height;
        result->line_count = static_cast<size_t>(measured.line_count);
      };
      auto set_length = [&](double value) {
        result->result_kind = MERMAN_TEXT_MEASUREMENT_RESULT_KIND_LENGTH;
        result->length = value;
      };

      switch (request->operation) {
        case MERMAN_TEXT_MEASUREMENT_OPERATION_MEASURE:
        case MERMAN_TEXT_MEASUREMENT_OPERATION_WRAPPED:
        case MERMAN_TEXT_MEASUREMENT_OPERATION_MERMAID_CALCULATE_TEXT_DIMENSIONS:
          set_metrics();
          break;
        case MERMAN_TEXT_MEASUREMENT_OPERATION_WRAPPED_WITH_RAW_WIDTH:
          set_metrics();
          result->result_kind =
              MERMAN_TEXT_MEASUREMENT_RESULT_KIND_WRAPPED_WITH_RAW_WIDTH;
          result->raw_width = measured.raw_width;
          result->has_raw_width = 1;
          break;
        case MERMAN_TEXT_MEASUREMENT_OPERATION_COMPUTED_LENGTH:
        case MERMAN_TEXT_MEASUREMENT_OPERATION_SIMPLE_BBOX_WIDTH:
        case MERMAN_TEXT_MEASUREMENT_OPERATION_RAW_BBOX_WIDTH:
        case MERMAN_TEXT_MEASUREMENT_OPERATION_TSPAN_BBOX_WIDTH:
        case MERMAN_TEXT_MEASUREMENT_OPERATION_WRAP_PROBE_BBOX_WIDTH:
        case MERMAN_TEXT_MEASUREMENT_OPERATION_BOUNDING_CLIENT_RECT_WIDTH:
        case MERMAN_TEXT_MEASUREMENT_OPERATION_CANVAS_MEASURE_TEXT_WIDTH:
          set_length(measured.width);
          break;
        case MERMAN_TEXT_MEASUREMENT_OPERATION_TSPAN_BBOX_HEIGHT:
        case MERMAN_TEXT_MEASUREMENT_OPERATION_SIMPLE_BBOX_HEIGHT:
        case MERMAN_TEXT_MEASUREMENT_OPERATION_RAW_BBOX_HEIGHT:
          set_length(measured.height);
          break;
        default:
          return MERMAN_NATIVE_STATUS_OK;
      }
      result->handled = 1;
      return MERMAN_NATIVE_STATUS_OK;
    } catch (...) {
      result->handled = 0;
      return MERMAN_NATIVE_STATUS_CALLBACK_ERROR;
    }
  }

  std::shared_ptr<const MdFontMetrics> metrics;
  MermanNativeApi api{};
  bool api_ready = false;
  bool loaded = false;
  bool permanently_failed = false;
  alignas(8) MermanNativeEngineToken engines[2] = {0, 0};
  std::string error;
  std::string version;
};

#endif  // QLITEMD_MDHTML_MERMAID_MERMAN

#if defined(QLITEMD_MDHTML_QUICKJS)

namespace {

// per-diagram theme override: mermaid.render() has no per-call config, but
// %%{init}%% applies to one render only.
std::string ThemeDirective(MdTheme theme) {
  if (theme == MdTheme::Light) {
    return {};
  }
  return "%%{init: {'theme': 'dark'}}%%\n";
}

// YAML frontmatter must stay first; insert the directive after its closing
// line when present.
std::string ApplyThemeDirective(std::string code, MdTheme theme) {
  const std::string directive = ThemeDirective(theme);
  if (directive.empty()) {
    return code;
  }
  const std::size_t first_line_end = code.find('\n');
  if (first_line_end != std::string::npos &&
      TrimView(code.substr(0, first_line_end)) == "---") {
    std::size_t line_start = first_line_end + 1;
    while (line_start <= code.size()) {
      const std::size_t line_end = code.find('\n', line_start);
      const std::size_t end =
          line_end == std::string::npos ? code.size() : line_end;
      if (TrimView(code.substr(line_start, end - line_start)) == "---") {
        if (line_end == std::string::npos) {
          return code + '\n' + directive;
        }
        const std::size_t insert_at = line_end + 1;
        return code.substr(0, insert_at) + directive + code.substr(insert_at);
      }
      if (line_end == std::string::npos) {
        break;
      }
      line_start = line_end + 1;
    }
  }
  return directive + code;
}

}  // namespace

// QuickJS backend: mermaid.js in a per-worker runtime with a DOM shim and
// injected text measurement.
class MdMermaidJs {
 public:
  explicit MdMermaidJs(std::shared_ptr<const MdFontMetrics> shared)
      : metrics(std::move(shared)) {
    if (!metrics) {
      metrics = std::make_shared<MdDefaultFontMetrics>();
    }
  }

  bool EnsureLoaded() {
    if (runtime.status().loaded) {
      return true;
    }
    if (runtime.status().permanently_failed) {
      return false;
    }
    // C callbacks find the engine state through the runtime opaque slot.
    runtime.SetRuntimeOpaque(this);
    runtime.set_on_fail([this](JSContext*) { initialized = false; });
    runtime.set_preserve_first_load(true);
    return runtime.EnsureLoaded(
        {{md_pb, md_pb_size, "path_bbox.js"},
         {md_ds, md_ds_size, "dom_shim.js"},
         {md_mm, md_mm_size, "mermaid.js"}},
        [this](JSContext* context) {
          // dom_shim expects these bridge functions before the payloads.
          JSValue global = JS_GetGlobalObject(context);
          JS_SetPropertyStr(context, global, "__log_raw",
                            JS_NewCFunction(context, JsLogRaw, "__log_raw", 1));
          JS_SetPropertyStr(context, global, "__measureText_raw",
                            JS_NewCFunction(context, JsMeasureTextRaw,
                                            "__measureText_raw", 5));
          JS_SetPropertyStr(context, global, "__measureTextFull_raw",
                            JS_NewCFunction(context, JsMeasureTextFullRaw,
                                            "__measureTextFull_raw", 5));
          JS_SetPropertyStr(
              context, global, "__setSettled_raw",
              JS_NewCFunction(context, JsSetSettledRaw, "__setSettled_raw", 0));
          JS_FreeValue(context, global);
          static const char* const kBridge =
              "globalThis.__log = (s) => __log_raw(s);\n"
              "globalThis.__measureText = (t,s,f,w,st) => "
              "__measureText_raw(t,s,f,w,st);\n"
              "globalThis.__measureTextFull = (t,s,f,w,st) => "
              "JSON.parse(__measureTextFull_raw(t,s,f,w,st));\n";
          return runtime.Eval(kBridge, "bridge", nullptr);
        },
        [this](JSContext*) {
          // mermaid.js is an esbuild ESM bundle; expose its default manually.
          static const char* const kExpose =
              "globalThis.mermaid = "
              "(globalThis.__esbuild_esm_mermaid_nm.mermaid.default"
              " || globalThis.__esbuild_esm_mermaid_nm.mermaid);";
          return runtime.Eval(kExpose, "expose", nullptr);
        });
  }

  MdMermaidResult Render(std::string_view code, MdTheme theme) {
    MdMermaidResult out;
    if (!EnsureLoaded() || !EnsureInitialized()) {
      out.error = "mermaid engine failed to load";
      return out;
    }
    const std::string svg =
        RenderSvg(ApplyThemeDirective(std::string(code), theme));
    if (svg.empty()) {
      out.error = "mermaid render failed";
      return out;
    }
    return FinishSvg(std::move(out), svg);
  }

  void SetInterruptFlag(std::atomic<bool>* flag) {
    runtime.SetInterruptFlag(flag);
  }

 private:
  static JSValue JsSetSettledRaw(JSContext* context, JSValue, int, JSValue*) {
    auto* engine =
        static_cast<MdMermaidJs*>(JS_GetRuntimeOpaque(JS_GetRuntime(context)));
    if (engine != nullptr) {
      engine->render_settled = true;
    }
    return JS_UNDEFINED;
  }

  static JSValue JsLogRaw(JSContext*, JSValue, int, JSValue*) {
    return JS_UNDEFINED;
  }

  static JSValue JsMeasureTextRaw(JSContext* context, JSValue, int argc,
                                  JSValue* argv) {
    if (argc < 5 || context == nullptr) {
      return JS_NewFloat64(context, 0.0);
    }
    auto* engine =
        static_cast<MdMermaidJs*>(JS_GetRuntimeOpaque(JS_GetRuntime(context)));
    if (engine == nullptr || !engine->metrics) {
      return JS_NewFloat64(context, 0.0);
    }
    const char* text = JS_ToCString(context, argv[0]);
    double size = 16.0;
    JS_ToFloat64(context, &size, argv[1]);
    const char* family = JS_ToCString(context, argv[2]);
    const char* weight = JS_ToCString(context, argv[3]);
    const char* style = JS_ToCString(context, argv[4]);
    const MeasuredText measured = MeasureText(
        text != nullptr ? text : "", family != nullptr ? family : "", size,
        weight != nullptr ? weight : "", style != nullptr ? style : "", false,
        0.0, 0.0, *engine->metrics);
    if (text != nullptr) {
      JS_FreeCString(context, text);
    }
    if (family != nullptr) {
      JS_FreeCString(context, family);
    }
    if (weight != nullptr) {
      JS_FreeCString(context, weight);
    }
    if (style != nullptr) {
      JS_FreeCString(context, style);
    }
    return JS_NewFloat64(context, measured.width);
  }

  static JSValue JsMeasureTextFullRaw(JSContext* context, JSValue, int argc,
                                      JSValue* argv) {
    if (argc < 5 || context == nullptr) {
      return JS_NewString(context, "{\"width\":0,\"ascent\":0,\"descent\":0}");
    }
    auto* engine =
        static_cast<MdMermaidJs*>(JS_GetRuntimeOpaque(JS_GetRuntime(context)));
    if (engine == nullptr || !engine->metrics) {
      return JS_NewString(context, "{\"width\":0,\"ascent\":0,\"descent\":0}");
    }
    const char* text = JS_ToCString(context, argv[0]);
    double size = 16.0;
    JS_ToFloat64(context, &size, argv[1]);
    const char* family = JS_ToCString(context, argv[2]);
    const char* weight = JS_ToCString(context, argv[3]);
    const char* style = JS_ToCString(context, argv[4]);
    const MeasuredText measured = MeasureText(
        text != nullptr ? text : "", family != nullptr ? family : "", size,
        weight != nullptr ? weight : "", style != nullptr ? style : "", false,
        0.0, 0.0, *engine->metrics);
    if (text != nullptr) {
      JS_FreeCString(context, text);
    }
    if (family != nullptr) {
      JS_FreeCString(context, family);
    }
    if (weight != nullptr) {
      JS_FreeCString(context, weight);
    }
    if (style != nullptr) {
      JS_FreeCString(context, style);
    }
    char buffer[192];
    std::snprintf(buffer, sizeof(buffer),
                  "{\"width\":%.6f,\"ascent\":%.6f,\"descent\":%.6f}",
                  measured.width, measured.ascent, measured.descent);
    return JS_NewString(context, buffer);
  }

  bool EnsureInitialized() {
    if (initialized) {
      return true;
    }
    // htmlLabels:false keeps text out of <foreignObject>, which no SVG
    // rasterizer in this project understands. initialize replaces config, so
    // every option is listed here; the theme stays per-diagram.
    static const char* const kInit =
        "mermaid.initialize({"
        "  startOnLoad: false,"
        "  htmlLabels: false,"
        "  flowchart: { htmlLabels: false },"
        "  class: { htmlLabels: false },"
        "  state: { htmlLabels: false },"
        "  er: { useMaxWidth: true }"
        "});";
    if (!runtime.Eval(kInit, "init", nullptr)) {
      runtime.Abort();
      return false;
    }
    initialized = true;
    return true;
  }

  std::string RenderSvg(std::string_view code) {
    JSContext* context = runtime.context();
    if (!runtime.Eval("__resetDocument();", "reset", nullptr)) {
      return {};
    }

    JSValue global = JS_GetGlobalObject(context);
    JS_SetPropertyStr(context, global, "__code",
                      JS_NewStringLen(context, code.data(), code.size()));
    JS_SetPropertyStr(context, global, "__renderId",
                      JS_NewString(context, "m0"));
    JS_FreeValue(context, global);

    // The C flag is the completion signal: both then and catch must set it,
    // otherwise a rejected render waits out the whole pump budget.
    static const char* const kCall =
        "globalThis.__renderResult = null;\n"
        "globalThis.__renderError = null;\n"
        "mermaid.render(__renderId, __code)\n"
        "  .then(r => { globalThis.__renderResult = r.svg || '';\n"
        "               __setSettled_raw(); })\n"
        "  .catch(e => { globalThis.__renderError = "
        "      (e && e.name ? e.name + ': ' + e.message : String(e));\n"
        "                 __setSettled_raw(); });";
    render_settled = false;
    if (!runtime.Eval(kCall, "render-call", nullptr)) {
      return {};
    }

    constexpr int kMaxJobs = 200000;
    constexpr auto kPumpDeadline = std::chrono::seconds(30);
    const auto pump_start = std::chrono::steady_clock::now();
    bool done = false;
    for (int i = 0; i < kMaxJobs; ++i) {
      JSContext* pump_context = nullptr;
      const int result = JS_ExecutePendingJob(runtime.runtime(), &pump_context);
      if (result < 0) {
        JS_FreeValue(context, JS_GetException(context));
        break;
      }
      if (result == 0) {
        break;
      }
      if (std::chrono::steady_clock::now() - pump_start > kPumpDeadline) {
        break;
      }
      if (render_settled) {
        done = true;
        break;
      }
    }
    if (!done) {
      return {};
    }

    global = JS_GetGlobalObject(context);
    JSValue error = JS_GetPropertyStr(context, global, "__renderError");
    if (!JS_IsNull(error) && !JS_IsUndefined(error)) {
      JS_FreeValue(context, error);
      JS_FreeValue(context, global);
      return {};
    }
    JS_FreeValue(context, error);

    std::string svg;
    JSValue value = JS_GetPropertyStr(context, global, "__renderResult");
    if (!JS_IsNull(value) && !JS_IsUndefined(value)) {
      const char* text = JS_ToCString(context, value);
      if (text != nullptr) {
        svg = text;
        JS_FreeCString(context, text);
      }
    }
    JS_FreeValue(context, value);
    JS_FreeValue(context, global);
    return svg;
  }

  std::shared_ptr<const MdFontMetrics> metrics;
  MdJsRuntime runtime;
  bool initialized = false;
  bool render_settled = false;
};

#endif  // QLITEMD_MDHTML_QUICKJS

struct MdMermaidEngine::Impl {
  explicit Impl(std::shared_ptr<const MdFontMetrics> shared)
      : metrics(std::move(shared)) {
    if (!metrics) {
      metrics = std::make_shared<MdDefaultFontMetrics>();
    }
  }

  std::shared_ptr<const MdFontMetrics> metrics;
#if defined(QLITEMD_MDHTML_MERMAID_MERMAN)
  std::optional<MdMermaidNative> native;
#endif
#if defined(QLITEMD_MDHTML_QUICKJS)
  std::optional<MdMermaidJs> js;
  std::atomic<bool>* interrupt_flag = nullptr;
#endif

#if defined(QLITEMD_MDHTML_MERMAID_MERMAN)
  MdMermaidNative& Native() {
    if (!native.has_value()) {
      native.emplace(metrics);
    }
    return *native;
  }
#endif
#if defined(QLITEMD_MDHTML_QUICKJS)
  MdMermaidJs& Js() {
    if (!js.has_value()) {
      js.emplace(metrics);
      if (interrupt_flag != nullptr) {
        // A native attempt may have failed before this lazy fallback; the
        // flag was already set on the engine, so hand it to the new runtime.
        js->SetInterruptFlag(interrupt_flag);
      }
    }
    return *js;
  }
#endif

  // Tries one backend; prefixes the error with its name so a fallback that
  // also fails reports both causes.
  bool TryNative(std::string_view code, MdTheme theme, MdMermaidResult* out) {
#if defined(QLITEMD_MDHTML_MERMAID_MERMAN)
    *out = Native().Render(code, theme);
    if (out->ok) {
      out->backend_used = MdMermaidBackend::kNative;
      return true;
    }
    out->error = "merman: " + out->error;
    return false;
#else
    (void)code;
    (void)theme;
    (void)out;
    return false;
#endif
  }

  bool TryJs(std::string_view code, MdTheme theme, MdMermaidResult* out) {
#if defined(QLITEMD_MDHTML_QUICKJS)
    *out = Js().Render(code, theme);
    if (out->ok) {
      out->backend_used = MdMermaidBackend::kQuickjs;
      return true;
    }
    out->error = "quickjs: " + out->error;
    return false;
#else
    (void)code;
    (void)theme;
    (void)out;
    return false;
#endif
  }

  MdMermaidResult Render(std::string_view code, MdTheme theme,
                         MdMermaidBackend mode, MdMermaidBackend* used) {
    constexpr bool kHasNative =
#if defined(QLITEMD_MDHTML_MERMAID_MERMAN)
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

    // auto tries merman first, then falls back per diagram to mermaid.js.
    // An explicit request is pinned to its backend; it only degrades to the
    // other engine when that backend was not compiled in at all.
    MdMermaidResult out;
    auto accept = [&]() {
      if (used != nullptr) {
        *used = out.backend_used;
      }
      return out;
    };

    if (mode == MdMermaidBackend::kNative) {
      if (kHasNative) {
        if (TryNative(code, theme, &out)) {
          return accept();
        }
      } else if (kHasJs && TryJs(code, theme, &out)) {
        return accept();
      }
      if (!out.ok && out.error.empty()) {
        out.error = "mermaid backend unavailable in this build";
      }
      return out;
    }
    if (mode == MdMermaidBackend::kQuickjs) {
      if (kHasJs) {
        if (TryJs(code, theme, &out)) {
          return accept();
        }
      } else if (kHasNative && TryNative(code, theme, &out)) {
        return accept();
      }
      if (!out.ok && out.error.empty()) {
        out.error = "mermaid backend unavailable in this build";
      }
      return out;
    }

    std::string native_error;
    if (kHasNative) {
      if (TryNative(code, theme, &out)) {
        return accept();
      }
      native_error = out.error;
    }
    if (kHasJs) {
      if (TryJs(code, theme, &out)) {
        return accept();
      }
      if (!native_error.empty()) {
        out.error = native_error + "; " + out.error;
      }
      return out;
    }
    out.error = native_error.empty()
                    ? "mermaid backend unavailable in this build"
                    : native_error;
    return out;
  }

  bool EnsureLoaded() {
    bool ok = false;
#if defined(QLITEMD_MDHTML_MERMAID_MERMAN)
    ok = Native().EnsureLoaded() || ok;
#endif
#if defined(QLITEMD_MDHTML_QUICKJS)
    ok = Js().EnsureLoaded() || ok;
#endif
    return ok;
  }

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
};

bool MdMermaidNativeAvailable() {
#if defined(QLITEMD_MDHTML_MERMAID_MERMAN)
  return true;
#else
  return false;
#endif
}

bool MdMermaidQuickJsAvailable() {
#if defined(QLITEMD_MDHTML_QUICKJS)
  return true;
#else
  return false;
#endif
}

MdMermaidEngine::MdMermaidEngine(std::shared_ptr<const MdFontMetrics> metrics)
    : impl_(new Impl(std::move(metrics))) {}

MdMermaidEngine::~MdMermaidEngine() = default;

void MdMermaidEngine::SetInterruptFlag(std::atomic<bool>* flag) {
  impl_->SetInterruptFlag(flag);
}

bool MdMermaidEngine::EnsureLoaded() { return impl_->EnsureLoaded(); }

MdMermaidResult MdMermaidEngine::Render(std::string_view code, MdTheme theme,
                                        MdMermaidBackend mode,
                                        MdMermaidBackend* backend_used) {
  return impl_->Render(code, theme, mode, backend_used);
}

const char* MdMermaidBackendName(MdMermaidBackend backend) {
  switch (backend) {
    case MdMermaidBackend::kNative:
      return "merman";
    case MdMermaidBackend::kQuickjs:
      return "quickjs";
    case MdMermaidBackend::kAuto:
      break;
  }
  return "auto";
}

std::string MdMermaidRenderKey(std::string_view code, MdTheme theme,
                               const MdFontMetrics& metrics,
                               MdMermaidBackend mode) {
  std::string key = ThemeName(theme);
  key.push_back('|');
  key += metrics.Fingerprint();
  std::string compiled;
  if (MdMermaidNativeAvailable()) {
    // Keep in sync with the merman VERSION.md; this busts persistent caches
    // when the native backend output changes.
    compiled = "merman-0.9.2";
  }
  if (MdMermaidQuickJsAvailable()) {
    compiled += compiled.empty() ? "" : "+";
    compiled += "quickjs";
  }
  if (compiled.empty()) {
    compiled = "none";
  }
  key += "|mm:";
  key += compiled;
  key.push_back(':');
  key += MdMermaidBackendName(mode);
  key.push_back('|');
  key.append(code);
  return key;
}
