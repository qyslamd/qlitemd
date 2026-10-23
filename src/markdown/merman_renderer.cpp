#include "merman_renderer.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>

#include <QBuffer>
#include <QByteArray>
#include <QDebug>
#include <QElapsedTimer>
#include <QFont>
#include <QFontDatabase>
#include <QFontMetricsF>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSizeF>
#include <QTextLayout>
#include <QTextLine>
#include <QTextOption>

#include "mermaid_renderer.h"
#include "merman.h"
#include "render_diagnostics.h"
#include "svg_rasterizer.h"

namespace {

QString SliceToString(const MermanNativeSlice& slice) {
  if (slice.data == nullptr || slice.len == 0) {
    return QString();
  }
  return QString::fromUtf8(reinterpret_cast<const char*>(slice.data),
                           static_cast<int>(slice.len));
}

QString BufferToString(const MermanNativeBuffer& buffer) {
  if (buffer.data == nullptr || buffer.len == 0) {
    return QString();
  }
  return QString::fromUtf8(reinterpret_cast<const char*>(buffer.data),
                           static_cast<int>(buffer.len));
}

MermanNativeSlice BorrowSlice(const QByteArray& bytes) {
  MermanNativeSlice slice;
  slice.struct_size = MERMAN_NATIVE_STRUCT_SIZE(MermanNativeSlice);
  slice.data = reinterpret_cast<const uint8_t*>(bytes.constData());
  slice.len = static_cast<size_t>(bytes.size());
  return slice;
}

QString StatusName(MermanNativeStatus status) {
  switch (status) {
    case MERMAN_NATIVE_STATUS_OK:
      return QStringLiteral("ok");
    case MERMAN_NATIVE_STATUS_INVALID_ARGUMENT:
      return QStringLiteral("invalid-argument");
    case MERMAN_NATIVE_STATUS_OPTIONS_JSON_ERROR:
      return QStringLiteral("options-json-error");
    case MERMAN_NATIVE_STATUS_NO_DIAGRAM:
      return QStringLiteral("no-diagram");
    case MERMAN_NATIVE_STATUS_PARSE_ERROR:
      return QStringLiteral("parse-error");
    case MERMAN_NATIVE_STATUS_RENDER_ERROR:
      return QStringLiteral("render-error");
    case MERMAN_NATIVE_STATUS_UNSUPPORTED_OPERATION:
      return QStringLiteral("unsupported-operation");
    case MERMAN_NATIVE_STATUS_BUSY:
      return QStringLiteral("busy");
    case MERMAN_NATIVE_STATUS_CANCELLED:
      return QStringLiteral("cancelled");
    default:
      return QStringLiteral("status-%1").arg(status);
  }
}

QString ErrorMessage(const MermanNativeResult& result) {
  const QString json = BufferToString(result.metadata_or_error_json);
  if (!json.isEmpty()) {
    const QJsonDocument document = QJsonDocument::fromJson(json.toUtf8());
    if (document.isObject()) {
      const QString message =
          document.object().value(QStringLiteral("message")).toString();
      if (!message.isEmpty()) {
        return message;
      }
    }
    return json;
  }
  return StatusName(result.status);
}

void AddArtifact(const MarkdownDiagnostics& diagnostics, const QString& name,
                 const QByteArray& data,
                 QVector<MarkdownRenderArtifact>* artifacts) {
  if (artifacts != nullptr) {
    artifacts->append(MarkdownRenderArtifact{name, data});
  } else {
    DumpRenderArtifact(diagnostics, name, data);
  }
}

QString PickFamily(const QString& css_list) {
  static const QFontDatabase database;
  QStringList families = css_list.split(QLatin1Char(','));
  const QString cjk = MermaidFontFamily();
  for (QString family : families) {
    family = family.trimmed();
    while (family.endsWith(QLatin1Char(';'))) {
      family.chop(1);
      family = family.trimmed();
    }
    if (family.size() >= 2 && ((family.startsWith(QLatin1Char('"')) &&
                                family.endsWith(QLatin1Char('"'))) ||
                               (family.startsWith(QLatin1Char('\'')) &&
                                family.endsWith(QLatin1Char('\''))))) {
      family = family.mid(1, family.size() - 2).trimmed();
    }
    if (family.compare(QLatin1String("sans-serif"), Qt::CaseInsensitive) == 0) {
      family = cjk.isEmpty() ? QStringLiteral("Arial") : cjk;
    } else if (family.compare(QLatin1String("serif"), Qt::CaseInsensitive) ==
               0) {
      family = QStringLiteral("Times New Roman");
    } else if (family.compare(QLatin1String("monospace"),
                              Qt::CaseInsensitive) == 0) {
      family = QStringLiteral("Consolas");
    }
    if (!family.isEmpty() && database.hasFamily(family)) {
      return family;
    }
  }
  return cjk.isEmpty() ? QStringLiteral("Arial") : cjk;
}

QFont MakeFont(const MermanNativeTextMeasureRequest& request) {
  QFont font;
  font.setFamily(PickFamily(SliceToString(request.font_family)));
  font.setPixelSize(qMax(1, qRound(request.font_size)));
  const QString weight = SliceToString(request.font_weight).toLower();
  if (weight == QLatin1String("bold") || weight == QLatin1String("bolder") ||
      weight.toInt() >= 600) {
    font.setWeight(QFont::Bold);
  }
  const QString style = SliceToString(request.font_style).toLower();
  if (style == QLatin1String("italic") || style == QLatin1String("oblique")) {
    font.setItalic(true);
  }
  return font;
}

struct TextMetrics {
  qreal width = 0.0;
  qreal height = 0.0;
  int line_count = 1;
  qreal raw_width = 0.0;
};

TextMetrics MeasureText(const MermanNativeTextMeasureRequest& request,
                        const QFont& font) {
  const QString text = SliceToString(request.text);
  const QFontMetricsF metrics(font);
  TextMetrics result;
  result.raw_width = metrics.horizontalAdvance(text);
  result.width = result.raw_width;
  result.height = request.line_height;
  if (request.has_max_width && request.max_width > 0.0 &&
      request.wrap_mode != MERMAN_TEXT_WRAP_MODE_SVG_LIKE_SINGLE_RUN) {
    QTextOption option;
    option.setWrapMode(request.white_space == MERMAN_TEXT_WHITE_SPACE_NOWRAP
                           ? QTextOption::NoWrap
                           : QTextOption::WrapAtWordBoundaryOrAnywhere);
    QTextLayout layout(text, font);
    layout.setTextOption(option);
    layout.beginLayout();
    qreal widest = 0.0;
    int lines = 0;
    while (true) {
      QTextLine line = layout.createLine();
      if (!line.isValid()) {
        break;
      }
      line.setLineWidth(request.max_width);
      line.setPosition(QPointF(0.0, lines * request.line_height));
      widest = qMax(widest, line.naturalTextWidth());
      ++lines;
    }
    layout.endLayout();
    if (lines > 0) {
      result.line_count = lines;
      result.width = widest;
      result.height = lines * request.line_height;
    }
  }
  return result;
}

MermanNativeStatus MeasureTextCallback(
    const MermanNativeTextMeasureRequest* request,
    MermanNativeTextMeasureResult* result, void* /*user_data*/) noexcept {
  if (request == nullptr || result == nullptr) {
    return MERMAN_NATIVE_STATUS_INVALID_ARGUMENT;
  }
  try {
    result->struct_size =
        MERMAN_NATIVE_STRUCT_SIZE(MermanNativeTextMeasureResult);
    result->handled = 0;
    result->has_raw_width = 0;
    result->raw_width = 0.0;

    const QFont font = MakeFont(*request);
    const TextMetrics metrics = MeasureText(*request, font);
    auto set_metrics = [&]() {
      result->result_kind = MERMAN_TEXT_MEASUREMENT_RESULT_KIND_METRICS;
      result->width = metrics.width;
      result->height = metrics.height;
      result->line_count = static_cast<size_t>(metrics.line_count);
    };
    auto set_length = [&](qreal value) {
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
        result->raw_width = metrics.raw_width;
        result->has_raw_width = 1;
        break;
      case MERMAN_TEXT_MEASUREMENT_OPERATION_COMPUTED_LENGTH:
      case MERMAN_TEXT_MEASUREMENT_OPERATION_SIMPLE_BBOX_WIDTH:
      case MERMAN_TEXT_MEASUREMENT_OPERATION_RAW_BBOX_WIDTH:
      case MERMAN_TEXT_MEASUREMENT_OPERATION_TSPAN_BBOX_WIDTH:
      case MERMAN_TEXT_MEASUREMENT_OPERATION_WRAP_PROBE_BBOX_WIDTH:
      case MERMAN_TEXT_MEASUREMENT_OPERATION_BOUNDING_CLIENT_RECT_WIDTH:
      case MERMAN_TEXT_MEASUREMENT_OPERATION_CANVAS_MEASURE_TEXT_WIDTH:
        set_length(metrics.width);
        break;
      case MERMAN_TEXT_MEASUREMENT_OPERATION_TSPAN_BBOX_HEIGHT:
      case MERMAN_TEXT_MEASUREMENT_OPERATION_SIMPLE_BBOX_HEIGHT:
      case MERMAN_TEXT_MEASUREMENT_OPERATION_RAW_BBOX_HEIGHT:
        set_length(metrics.height);
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

QByteArray EngineOptions(MarkdownTheme theme) {
  const QString family = MermaidFontFamily();
  QJsonObject site;
  site.insert(QStringLiteral("theme"), theme == MarkdownTheme::Dark
                                           ? QStringLiteral("dark")
                                           : QStringLiteral("default"));
  site.insert(QStringLiteral("look"), QStringLiteral("classic"));
  if (!family.isEmpty()) {
    site.insert(QStringLiteral("fontFamily"), family);
    QJsonObject variables;
    variables.insert(QStringLiteral("fontFamily"), family);
    site.insert(QStringLiteral("themeVariables"), variables);
  }
  QJsonObject root;
  root.insert(QStringLiteral("site_config"), site);
  return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

QString MermanSvgToImgTag(const QString& svg,
                          const MarkdownDiagnostics& diagnostics,
                          QVector<MarkdownRenderArtifact>* artifacts) {
  static std::atomic<int> dump_index{0};
  const int index = dump_index.fetch_add(1, std::memory_order_relaxed) + 1;

  const int start = svg.indexOf(QLatin1String("<svg"));
  const int end = svg.lastIndexOf(QLatin1String("</svg>"));
  if (start < 0 || end < 0) {
    if (diagnostics.trace) {
      TraceRender(diagnostics, QStringLiteral("merman svg: no <svg> root"));
    }
    return QString();
  }
  const QString bare = svg.mid(start, end - start + 6);
  const QString raw_name = QStringLiteral("mermaid/%1-merman.svg").arg(index);
  if (DumpEnabled(diagnostics, raw_name)) {
    AddArtifact(diagnostics, raw_name, bare.toUtf8(), artifacts);
  }

  static const QRegularExpression kViewBox(
      QStringLiteral("viewBox=\"([-\\d.]+) ([-\\d.]+) ([\\d.]+) ([\\d.]+)\""));
  const QRegularExpressionMatch match = kViewBox.match(bare);
  qreal width = 0.0;
  qreal height = 0.0;
  if (match.hasMatch()) {
    width = match.captured(3).toDouble();
    height = match.captured(4).toDouble();
  }
  if (width < 1.0 || height < 1.0) {
    QSizeF intrinsic;
    QString size_error;
    if (!SvgIntrinsicSize(bare.toUtf8(), MermaidFontFamily(), &intrinsic,
                          &size_error)) {
      if (diagnostics.trace) {
        TraceRender(diagnostics,
                    QStringLiteral("merman svg: %1").arg(size_error));
      }
      return QString();
    }
    width = intrinsic.width();
    height = intrinsic.height();
  }
  if (width < 1.0 || height < 1.0) {
    if (diagnostics.trace) {
      TraceRender(
          diagnostics,
          QStringLiteral("merman svg: bad size %1x%2").arg(width).arg(height));
    }
    return QString();
  }

  qreal scale = 2.0;
  constexpr qreal kMaxRasterPixels = 4096.0 * 4096.0;
  const qreal area = width * height;
  if (area * scale * scale > kMaxRasterPixels) {
    scale = std::sqrt(kMaxRasterPixels / area);
  }
  QImage image;
  QString raster_error;
  if (!RasterizeSvg(bare.toUtf8(), QSizeF(width, height), scale,
                    MermaidFontFamily(), &image, &raster_error)) {
    qWarning() << "merman: rasterize failed:" << raster_error;
    if (diagnostics.trace) {
      TraceRender(diagnostics,
                  QStringLiteral("merman svg: %1").arg(raster_error));
    }
    return QString();
  }

  QBuffer buffer;
  buffer.open(QIODevice::WriteOnly);
  if (!image.save(&buffer, "PNG")) {
    if (diagnostics.trace) {
      TraceRender(diagnostics, QStringLiteral("merman png: save failed"));
    }
    return QString();
  }
  const QString png_name = QStringLiteral("mermaid/%1-merman.png").arg(index);
  if (DumpEnabled(diagnostics, png_name)) {
    AddArtifact(diagnostics, png_name, buffer.data(), artifacts);
  }
  return QStringLiteral(
             "<img class=\"mermaid\" src=\"data:image/png;base64,%1\" "
             "width=\"%2\" height=\"%3\">")
      .arg(QString::fromLatin1(buffer.data().toBase64()))
      .arg(qRound(width))
      .arg(qRound(height));
}

}  // namespace

struct MermanEngine::Impl {
  MermanNativeApi api{};
  bool api_ready = false;
  alignas(8) MermanNativeEngineToken engines[2] = {0, 0};
  bool loaded = false;
  bool permanently_failed = false;
  qint64 first_load_ms = 0;
  QString error;
  QString version;

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
};

MermanEngine::MermanEngine() : impl_(std::make_unique<Impl>()) {}

MermanEngine::~MermanEngine() = default;

bool MermanEngine::EnsureLoaded() {
  if (impl_->loaded) {
    return true;
  }
  if (impl_->permanently_failed) {
    return false;
  }
  QElapsedTimer clock;
  clock.start();

  MermanNativeApiRequest request{};
  request.struct_size = MERMAN_NATIVE_STRUCT_SIZE(MermanNativeApiRequest);
  request.expected_abi_version = MERMAN_NATIVE_ABI_VERSION;
  const char* digest = MERMAN_NATIVE_ABI_MINIMUM_PREFIX_LAYOUT_DIGEST;
  request.expected_minimum_prefix_layout_digest.data =
      reinterpret_cast<const uint8_t*>(digest);
  request.expected_minimum_prefix_layout_digest.len = std::strlen(digest);
  request.expected_minimum_prefix_layout_digest.struct_size =
      MERMAN_NATIVE_STRUCT_SIZE(MermanNativeSlice);

  impl_->api.struct_size = MERMAN_NATIVE_STRUCT_SIZE(MermanNativeApi);
  if (merman_get_native_api(&request, &impl_->api) != MERMAN_NATIVE_STATUS_OK) {
    impl_->permanently_failed = true;
    impl_->error = QStringLiteral("merman ABI discovery failed");
    qWarning() << "merman:" << impl_->error;
    return false;
  }
  impl_->api_ready = true;
  impl_->version = SliceToString(impl_->api.package_version);

  for (int index = 0; index < 2; ++index) {
    const QByteArray options =
        EngineOptions(index == 0 ? MarkdownTheme::Light : MarkdownTheme::Dark);
    MermanNativeEngineConfig config{};
    config.struct_size = MERMAN_NATIVE_STRUCT_SIZE(MermanNativeEngineConfig);
    config.options_json = BorrowSlice(options);
    config.text_measure = &MeasureTextCallback;
    config.text_measure_user_data = nullptr;
    // MermanNativeResult contains a uint64 allocation token; the x86 ABI
    // requires both the result record and out_engine to be 8-byte aligned.
    alignas(8) MermanNativeResult result = MERMAN_NATIVE_RESULT_INIT;
    const MermanNativeStatus status =
        impl_->api.engine_new(&config, &impl_->engines[index], &result);
    if (status != MERMAN_NATIVE_STATUS_OK ||
        result.status != MERMAN_NATIVE_STATUS_OK) {
      impl_->error = ErrorMessage(result);
      impl_->api.result_free(&result);
      impl_->permanently_failed = true;
      qWarning() << "merman: engine_new failed:" << impl_->error;
      return false;
    }
    impl_->api.result_free(&result);
  }
  impl_->loaded = true;
  impl_->first_load_ms = clock.elapsed();
  return true;
}

MarkdownEngineStatus MermanEngine::status() const {
  MarkdownEngineStatus status;
  status.loaded = impl_->loaded;
  status.permanently_failed = impl_->permanently_failed;
  status.first_load_ms = impl_->first_load_ms;
  return status;
}

QString MermanEngine::Render(const QString& code, MarkdownTheme theme,
                             const MarkdownDiagnostics& diagnostics,
                             QVector<MarkdownRenderArtifact>* artifacts) {
  if (!EnsureLoaded()) {
    if (diagnostics.trace) {
      TraceRender(diagnostics,
                  QStringLiteral("merman: not loaded: %1").arg(impl_->error));
    }
    return QString();
  }
  static std::atomic<uint64_t> diagram_id{0};
  const QByteArray source = code.toUtf8();
  const QByteArray options =
      QStringLiteral(
          "{\"svg\":{\"diagram_id\":\"qlitemd-%1\","
          "\"pipeline\":\"resvg-safe\"}}")
          .arg(diagram_id.fetch_add(1, std::memory_order_relaxed) + 1)
          .toUtf8();
  MermanNativeOperationRequest request{};
  request.struct_size = MERMAN_NATIVE_STRUCT_SIZE(MermanNativeOperationRequest);
  request.operation = MERMAN_NATIVE_OPERATION_SVG;
  request.source = BorrowSlice(source);
  request.uri.struct_size = MERMAN_NATIVE_STRUCT_SIZE(MermanNativeSlice);
  request.options_json = BorrowSlice(options);

  const size_t engine_index = theme == MarkdownTheme::Dark ? 1 : 0;
  alignas(8) MermanNativeResult result = MERMAN_NATIVE_RESULT_INIT;
  const MermanNativeStatus status = impl_->api.execute_collect(
      impl_->engines[engine_index], &request, &result);
  if (status != MERMAN_NATIVE_STATUS_OK ||
      result.status != MERMAN_NATIVE_STATUS_OK) {
    const QString error = ErrorMessage(result);
    if (diagnostics.trace) {
      TraceRender(diagnostics,
                  QStringLiteral("merman render failed: %1").arg(error));
    }
    qWarning() << "merman render failed:" << error;
    impl_->api.result_free(&result);
    return QString();
  }
  const QString svg = BufferToString(result.data);
  impl_->api.result_free(&result);
  if (svg.isEmpty()) {
    if (diagnostics.trace) {
      TraceRender(diagnostics, QStringLiteral("merman render: empty svg"));
    }
    return QString();
  }
  return MermanSvgToImgTag(svg, diagnostics, artifacts);
}

MarkdownEngineStatus MermanEngineStatus(const MermanEngine& engine) {
  return engine.status();
}

QString MermanVersion() { return QStringLiteral("merman-0.9.2"); }
