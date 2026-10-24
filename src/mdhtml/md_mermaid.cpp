#include "md_mermaid.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>

#include "md_svg.h"
#include "md_text.h"

#if defined(QLITEMD_MDHTML_MERMAID_MERMAN)
#include "merman.h"
#endif

namespace {

const char* ThemeName(MdTheme theme) {
  return theme == MdTheme::Dark ? "dark" : "default";
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

int ParseWeight(std::string_view weight) {
  const std::string lower = ToLowerAscii(weight);
  weight = lower;
  if (weight == "bold" || weight == "bolder") {
    return 700;
  }
  if (weight == "lighter") {
    return 300;
  }
  int value = 0;
  bool has_digit = false;
  for (char c : weight) {
    if (std::isdigit(static_cast<unsigned char>(c)) == 0) {
      has_digit = false;
      break;
    }
    has_digit = true;
    value = value * 10 + (c - '0');
  }
  return has_digit ? value : 400;
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

struct MeasuredText {
  double width = 0.0;
  double height = 0.0;
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

MeasuredText MeasureText(const MermanNativeTextMeasureRequest& request,
                         const MdFontMetrics& metrics) {
  MdFontSpec font;
  font.family = metrics.ResolveFamily(SliceToString(request.font_family));
  font.size_px = std::max(1.0, request.font_size);
  font.weight = ParseWeight(SliceToString(request.font_weight));
  const std::string style = ToLowerAscii(SliceToString(request.font_style));
  font.italic = style == "italic" || style == "oblique";

  const std::string text = SliceToString(request.text);
  MdTextMetrics single;
  if (!text.empty()) {
    single = metrics.Measure(text, font);
  }

  MeasuredText measured;
  measured.raw_width = single.width;
  measured.width = single.width;
  const bool wrapping =
      request.has_max_width != 0 && request.max_width > 0.0 &&
      request.wrap_mode != MERMAN_TEXT_WRAP_MODE_SVG_LIKE_SINGLE_RUN;
  if (wrapping) {
    const WrapLayout layout = WrapText(text, font, request.max_width, metrics);
    measured.width = layout.width;
    measured.line_count = std::max(1, layout.lines);
  }
  const double line_height = request.line_height > 0.0
                                 ? request.line_height
                                 : single.ascent + single.descent;
  measured.height = measured.line_count * line_height;
  return measured;
}

}  // namespace

struct MdMermaidEngine::Impl {
  explicit Impl(std::shared_ptr<const MdFontMetrics> shared)
      : metrics(std::move(shared)) {
    if (!metrics) {
      metrics = std::make_shared<MdDefaultFontMetrics>();
    }
  }

  ~Impl() {
    if (api_ready && api.engine_try_close != nullptr) {
      for (MermanNativeEngineToken& token : engines) {
        if (token != 0) {
          api.engine_try_close(token);
          token = 0;
        }
      }
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

  bool EnsureLoaded();
  MdMermaidResult Render(std::string_view code, MdTheme theme);
};

namespace {

MermanNativeStatus MeasureTextCallback(
    const MermanNativeTextMeasureRequest* request,
    MermanNativeTextMeasureResult* result, void* user_data) noexcept {
  if (request == nullptr || result == nullptr || user_data == nullptr) {
    return MERMAN_NATIVE_STATUS_INVALID_ARGUMENT;
  }
  auto* impl = static_cast<MdMermaidEngine::Impl*>(user_data);
  try {
    result->struct_size =
        MERMAN_NATIVE_STRUCT_SIZE(MermanNativeTextMeasureResult);
    result->handled = 0;
    result->has_raw_width = 0;
    result->raw_width = 0.0;
    if (!impl->metrics) {
      return MERMAN_NATIVE_STATUS_INVALID_ARGUMENT;
    }

    const MeasuredText measured = MeasureText(*request, *impl->metrics);
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

}  // namespace

bool MdMermaidEngine::Impl::EnsureLoaded() {
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

MdMermaidResult MdMermaidEngine::Impl::Render(std::string_view code,
                                              MdTheme theme) {
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
  request.struct_size = MERMAN_NATIVE_STRUCT_SIZE(MermanNativeOperationRequest);
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

#else  // QLITEMD_MDHTML_MERMAID_MERMAN

struct MdMermaidEngine::Impl {
  explicit Impl(std::shared_ptr<const MdFontMetrics> shared)
      : metrics(std::move(shared)) {
    if (!metrics) {
      metrics = std::make_shared<MdDefaultFontMetrics>();
    }
  }
  std::shared_ptr<const MdFontMetrics> metrics;
};

#endif  // QLITEMD_MDHTML_MERMAID_MERMAN

MdMermaidEngine::MdMermaidEngine(std::shared_ptr<const MdFontMetrics> metrics)
    : impl_(new Impl(std::move(metrics))) {}

MdMermaidEngine::~MdMermaidEngine() = default;

bool MdMermaidEngine::EnsureLoaded() {
#if defined(QLITEMD_MDHTML_MERMAID_MERMAN)
  return impl_->EnsureLoaded();
#else
  return false;
#endif
}

MdMermaidResult MdMermaidEngine::Render(std::string_view code, MdTheme theme) {
#if defined(QLITEMD_MDHTML_MERMAID_MERMAN)
  return impl_->Render(code, theme);
#else
  (void)code;
  (void)theme;
  MdMermaidResult result;
  result.error = "mermaid backend unavailable in this build";
  return result;
#endif
}

std::string MdMermaidRenderKey(std::string_view code, MdTheme theme,
                               const MdFontMetrics& metrics) {
  std::string key = ThemeName(theme);
  key.push_back('|');
  key += metrics.Fingerprint();
#if defined(QLITEMD_MDHTML_MERMAID_MERMAN)
  key += "|merman-0.9.2";
#else
  key += "|quickjs";
#endif
  key.push_back('|');
  key.append(code);
  return key;
}
