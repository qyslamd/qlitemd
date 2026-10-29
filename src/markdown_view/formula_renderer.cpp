#include "formula_renderer.h"

#if !defined(QLITEMD_FORMULA_RATEX)
#include <quickjs.h>
#endif

#include <atomic>
#include <cmath>

#include <QBuffer>
#include <QDebug>
#include <QElapsedTimer>
#include <QImage>
#include <QRegularExpression>
#include <QSizeF>

#if defined(QLITEMD_FORMULA_RATEX)
#include "ratex_ffi.h"
#else
#include "js_runtime.h"
#include "markdown_bytecode.h"
#endif
#include "markdown_engine_state.h"
#include "markdown_render_context_internal.h"
#include "mermaid_renderer.h"
#include "render_cache.h"
#include "render_diagnostics.h"
#include "svg_rasterizer.h"

namespace {

QString FormulaBackendToken(MarkdownFormulaBackend backend) {
  switch (backend) {
    case MarkdownFormulaBackend::kNative:
      return QStringLiteral("ratex");
    case MarkdownFormulaBackend::kQuickjs:
      return QStringLiteral("quickjs");
    case MarkdownFormulaBackend::kAuto:
      break;
  }
  return QStringLiteral("auto");
}

QString FormulaNativeVersion() {
#if defined(QLITEMD_FORMULA_RATEX)
  // Keep in sync with 3rdparty/ratex/VERSION.md so an engine upgrade
  // invalidates the persistent PNG cache.
  return QStringLiteral("ratex-0.1.15");
#else
  return QStringLiteral("no-ratex");
#endif
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

// 2x 超采样、按 1x 尺寸摆放；像素总量封顶，防止超大公式打爆内存
RenderedImage RasterizeSizedSvg(const QString& svg, qreal width, qreal height,
                                const MarkdownDiagnostics& diagnostics,
                                const QString& artifact_prefix,
                                QVector<MarkdownRenderArtifact>* artifacts) {
  qreal scale = 2.0;
  constexpr qreal kMaxRasterPixels = 4096.0 * 4096.0;
  const qreal area = width * height;
  if (area * scale * scale > kMaxRasterPixels) {
    scale = std::sqrt(kMaxRasterPixels / area);
  }
  QImage image;
  QString raster_error;
  if (!RasterizeSvg(svg.toUtf8(), QSizeF(width, height), scale,
                    MermaidFontFamily(), &image, &raster_error)) {
    if (diagnostics.trace) {
      TraceRender(diagnostics,
                  QStringLiteral("formula png: %1").arg(raster_error));
    }
    return {};
  }
  QByteArray png;
  QBuffer buffer(&png);
  buffer.open(QIODevice::WriteOnly);
  if (!image.save(&buffer, "PNG")) {
    if (diagnostics.trace) {
      TraceRender(diagnostics, QStringLiteral("formula png: save failed"));
    }
    return {};
  }
  if (DumpEnabled(diagnostics, artifact_prefix)) {
    AddArtifact(diagnostics, artifact_prefix + QStringLiteral(".png"), png,
                artifacts);
  }
  RenderedImage out;
  out.image = std::move(image);
  out.png_bytes = png;
  out.layout_width = qMax(1, qRound(width));
  out.layout_height = qMax(1, qRound(height));
  return out;
}

#if !defined(QLITEMD_FORMULA_RATEX)

// 同步入口的线程局部引擎；异步 worker 自己持有 state
FormulaEngineState& DefaultEngine() {
  thread_local FormulaEngineState engine;
  return engine;
}

// SVG 字符串 → 渲染产物。失败返回 isNull() 的 RenderedImage。
// color 有效时替换 currentColor
RenderedImage SvgToImage(const QString& svg, const QColor& color,
                         const MarkdownDiagnostics& diagnostics,
                         const QString& artifact_prefix,
                         QVector<MarkdownRenderArtifact>* artifacts) {
  // MathJax 的输出包在 <mjx-container> 里，而 QSvgRenderer 只认 svg 根元素
  const int svg_start = svg.indexOf(QLatin1String("<svg"));
  const int svg_end = svg.lastIndexOf(QLatin1String("</svg>"));
  if (svg_start < 0 || svg_end < 0) {
    if (diagnostics.trace) {
      TraceRender(diagnostics, QStringLiteral("formula svg: no <svg> root"));
    }
    return {};
  }
  QString bare = svg.mid(svg_start, svg_end - svg_start + 6);
  if (color.isValid()) {
    // MathJax 常规输出在根 <g> 上写 fill/stroke="currentColor"；万一某版
    // 载荷不再写它，退回给根 <g> 补 fill，否则暗色主题下公式仍是黑字
    if (bare.contains(QLatin1String("currentColor"))) {
      bare.replace(QLatin1String("currentColor"), color.name());
    } else {
      const QString fill = QStringLiteral(" fill=\"%1\"").arg(color.name());
      int g_start = bare.indexOf(QLatin1String("<g "));
      if (g_start < 0) {
        g_start = bare.indexOf(QLatin1String("<g>"));
      }
      const int tag_start = g_start >= 0 ? g_start : 0;
      const int tag_end = bare.indexOf(QLatin1Char('>'), tag_start);
      // 已有 fill 时不补：重复属性在 XML 里非法，QSvgRenderer 取哪个也
      // 不可靠；显式配色说明载荷自带颜色，交给它
      static const QRegularExpression kFillAttribute(
          QStringLiteral("\\bfill\\s*="));
      const bool has_fill =
          tag_end > tag_start &&
          bare.mid(tag_start, tag_end - tag_start).contains(kFillAttribute);
      // 自闭合标签（<g/>）或结构异常时同样不动
      if (tag_end > tag_start && bare.at(tag_end - 1) != QLatin1Char('/') &&
          !has_fill) {
        bare.insert(tag_end, fill);
      }
    }
  }

  // MathJax 的 width/height 是 ex 单位；打包时约定 em=16px、ex=8px
  static const QRegularExpression kSizeExpression(
      QStringLiteral("width=\"([\\d.]+)ex\"\\s+height=\"([\\d.]+)ex\""));
  const QRegularExpressionMatch size = kSizeExpression.match(bare);
  qreal width = 0.0;
  qreal height = 0.0;
  if (size.hasMatch()) {
    width = size.captured(1).toDouble() * 8.0;
    height = size.captured(2).toDouble() * 8.0;
  } else {
    QSizeF intrinsic;
    QString size_error;
    if (!SvgIntrinsicSize(bare.toUtf8(), MermaidFontFamily(), &intrinsic,
                          &size_error)) {
      if (diagnostics.trace) {
        TraceRender(diagnostics,
                    QStringLiteral("formula svg: %1").arg(size_error));
      }
      return {};
    }
    width = intrinsic.width() / 1000.0 * 16.0;
    height = intrinsic.height() / 1000.0 * 16.0;
  }
  if (width < 1.0 || height < 1.0) {
    if (diagnostics.trace) {
      TraceRender(
          diagnostics,
          QStringLiteral("formula svg: bad size %1x%2").arg(width).arg(height));
    }
    return {};
  }
  return RasterizeSizedSvg(bare, width, height, diagnostics, artifact_prefix,
                           artifacts);
}

#endif  // !QLITEMD_FORMULA_RATEX

}  // namespace

#if !defined(QLITEMD_FORMULA_RATEX)

bool EnsureFormulaEngine(FormulaEngineState& engine) {
  if (engine.runtime.status().loaded) {
    return true;
  }
  if (engine.runtime.status().permanently_failed) {
    return false;
  }
  return engine.runtime.EnsureLoaded(
      {JsPayload{md_mj, md_mj_size, "mathjax-svg.min.js"}}, nullptr, nullptr);
}

MarkdownEngineStatus FormulaEngineStatus(const FormulaEngineState& engine) {
  return engine.runtime.status();
}

MarkdownEngineStatus FormulaEngineStatus() {
  return FormulaEngineStatus(DefaultEngine());
}

RenderedImage RenderFormulaOnEngine(
    FormulaEngineState& engine, const QString& tex, bool display,
    const QColor& color, const MarkdownDiagnostics& diagnostics,
    QVector<MarkdownRenderArtifact>* artifacts) {
  const QString normalized = tex.trimmed();
  const bool was_loaded = engine.runtime.status().loaded;
  if (!EnsureFormulaEngine(engine)) {
    if (diagnostics.trace) {
      TraceRender(
          diagnostics,
          QStringLiteral("formula load failed: %1").arg(normalized.left(60)));
    }
    return {};
  }
  if (!was_loaded && diagnostics.trace) {
    // 冷加载单独记账：first_load_ms 已经算好，别混进下面的渲染耗时
    TraceRender(diagnostics, QStringLiteral("formula engine loaded in %1 ms")
                                 .arg(engine.runtime.status().first_load_ms));
  }
  JSContext* js = engine.runtime.context();
  QElapsedTimer render_clock;
  render_clock.start();

  // tex 要进单引号 JS 字符串：反斜杠、引号、换行都得转义。换行尤其不能漏——
  // 块级 $$ 是跨行收集的，裸换行会让 JS 直接语法错误，公式静默落到兜底
  QString escaped = normalized;
  escaped.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
  escaped.replace(QLatin1Char('\''), QStringLiteral("\\'"));
  escaped.replace(QLatin1Char('\n'), QStringLiteral("\\n"));
  escaped.replace(QLatin1Char('\r'), QStringLiteral("\\r"));

  // 包一层 try/catch：JS 异常与原生崩溃要分得清（前者能拿到 message）
  const QString call = QStringLiteral(
                           "try { 'SVGOK:' + MathJaxAdapter.convert('%1', %2) }"
                           " catch (e) { 'SVGERR: ' + "
                           "(e && e.message ? e.message : e) }")
                           .arg(escaped, display ? QStringLiteral("true")
                                                 : QStringLiteral("false"));
  const QByteArray utf8 = call.toUtf8();
  const JSValue value = JS_Eval(js, utf8.constData(), utf8.size(), "convert",
                                JS_EVAL_TYPE_GLOBAL);

  RenderedImage tag;
  QString error;
  if (!JsRuntime::CheckResult(js, value, QStringLiteral("formula convert"),
                              &error)) {
    if (diagnostics.trace) {
      TraceRender(diagnostics,
                  QStringLiteral("formula exception: %1").arg(error));
    }
  } else {
    const char* raw = JS_ToCString(js, value);
    if (raw == nullptr) {
      // 分配失败等情况下 QuickJS 返回 null，并可能留下 pending 异常
      qWarning() << "formula convert: JS_ToCString returned null";
      JS_FreeValue(js, JS_GetException(js));
      if (diagnostics.trace) {
        TraceRender(diagnostics,
                    QStringLiteral("formula convert: JS_ToCString null"));
      }
    } else {
      const QString converted = QString::fromUtf8(raw);
      JS_FreeCString(js, raw);
      if (converted.startsWith(QLatin1String("SVGOK:"))) {
        // worker 并发时用原子序号，避免产物同名互相覆盖
        static std::atomic<int> dump_index{0};
        const int index =
            dump_index.fetch_add(1, std::memory_order_relaxed) + 1;
        const QString variant =
            color.isValid() ? color.name() : QStringLiteral("legacy");
        const QString artifact =
            QStringLiteral("formula/%1-%2").arg(index).arg(variant);
        const QString svg = converted.mid(6);
        if (DumpEnabled(diagnostics, artifact)) {
          AddArtifact(diagnostics, artifact + QStringLiteral(".svg"),
                      svg.toUtf8(), artifacts);
        }
        tag = SvgToImage(svg, color, diagnostics, artifact, artifacts);
      } else {
        qWarning() << "formula convert failed:" << converted;
        if (diagnostics.trace) {
          TraceRender(diagnostics, QStringLiteral("formula convert failed: %1")
                                       .arg(converted.left(120)));
        }
      }
    }
  }
  JS_FreeValue(js, value);

  if (diagnostics.trace) {
    TraceRender(diagnostics,
                QStringLiteral("formula render: %1 ms, %2 bytes, display=%3")
                    .arg(render_clock.elapsed())
                    .arg(tag.png_bytes.size())
                    .arg(display));
  }
  return tag;
}

#else  // QLITEMD_FORMULA_RATEX

MarkdownEngineStatus FormulaEngineStatus(const FormulaEngineState& /*engine*/) {
  // RaTeX 是直接 FFI，没有引擎加载阶段：恒就绪
  return {/*loaded=*/true, /*permanently_failed=*/false, /*first_load_ms=*/0};
}

MarkdownEngineStatus FormulaEngineStatus() {
  FormulaEngineState engine;
  return FormulaEngineStatus(engine);
}

RenderedImage RenderFormulaOnEngine(
    FormulaEngineState& /*engine*/, const QString& tex, bool display,
    const QColor& color, const MarkdownDiagnostics& diagnostics,
    QVector<MarkdownRenderArtifact>* artifacts) {
  const QString normalized = tex.trimmed();
  if (normalized.isEmpty()) {
    return {};
  }
  QElapsedTimer render_clock;
  render_clock.start();

  RatexColor rgba{0.0f, 0.0f, 0.0f, 1.0f};
  RatexColor* color_ptr = nullptr;
  if (color.isValid()) {
    rgba.r = static_cast<float>(color.redF());
    rgba.g = static_cast<float>(color.greenF());
    rgba.b = static_cast<float>(color.blueF());
    rgba.a = static_cast<float>(color.alphaF());
    color_ptr = &rgba;
  }
  // font_size 与既有 MathJax 管线对齐（em=16px）；padding 0 避免公式周围
  // 多出白边；stroke 按 40→16 的比例缩放
  RatexOptions options{};
  options.struct_size = sizeof(RatexOptions);
  options.display_mode = display ? 1 : 0;
  options.color = color_ptr;
  options.font_size = 16.0;
  options.padding = 0.0;
  options.stroke_width = 0.6;

  const QByteArray utf8 = normalized.toUtf8();
  const RatexResult result = ratex_render_svg(utf8.constData(), &options);
  if (result.error_code != 0 || result.data == nullptr) {
    const char* error = ratex_get_last_error();
    qWarning() << "formula(ratex) failed:" << (error != nullptr ? error : "?");
    if (diagnostics.trace) {
      TraceRender(diagnostics,
                  QStringLiteral("formula(ratex) failed: %1")
                      .arg(QString::fromUtf8(error != nullptr ? error : "?")));
    }
    ratex_free_display_list(result.data);
    return {};
  }
  const QString svg = QString::fromUtf8(result.data);
  ratex_free_display_list(result.data);

  static const QRegularExpression kViewBox(
      QStringLiteral("viewBox=\"([-\\d.]+) ([-\\d.]+) ([\\d.]+) ([\\d.]+)\""));
  const QRegularExpressionMatch match = kViewBox.match(svg);
  if (!match.hasMatch()) {
    if (diagnostics.trace) {
      TraceRender(diagnostics,
                  QStringLiteral("formula(ratex) svg: no viewBox"));
    }
    return {};
  }
  const qreal width = match.captured(3).toDouble();
  const qreal height = match.captured(4).toDouble();
  if (width < 1.0 || height < 1.0) {
    if (diagnostics.trace) {
      TraceRender(diagnostics,
                  QStringLiteral("formula(ratex) svg: bad size %1x%2")
                      .arg(width)
                      .arg(height));
    }
    return {};
  }

  static std::atomic<int> dump_index{0};
  const int index = dump_index.fetch_add(1, std::memory_order_relaxed) + 1;
  const QString variant =
      color.isValid() ? color.name() : QStringLiteral("legacy");
  const QString artifact =
      QStringLiteral("formula/%1-%2").arg(index).arg(variant);
  if (DumpEnabled(diagnostics, artifact)) {
    AddArtifact(diagnostics, artifact + QStringLiteral(".svg"), svg.toUtf8(),
                artifacts);
  }
  RenderedImage image =
      RasterizeSizedSvg(svg, width, height, diagnostics, artifact, artifacts);
  if (diagnostics.trace) {
    TraceRender(diagnostics,
                QStringLiteral("formula(ratex) render: %1 ms, %2 bytes, "
                               "display=%3")
                    .arg(render_clock.elapsed())
                    .arg(image.png_bytes.size())
                    .arg(display));
  }
  return image;
}

#endif  // QLITEMD_FORMULA_RATEX

QString FormulaRenderKey(const QString& tex, bool display, const QColor& color,
                         MarkdownFormulaBackend backend) {
  // MathJax 在数学模式下对首尾空白不敏感：key 与渲染输入统一用 trim 后的
  // 文本，避免 "x" 与 " x " 生成两条内容相同的缓存
  const QString normalized = tex.trimmed();
  const QString variant =
      color.isValid() ? color.name() : QStringLiteral("legacy");
  return (display ? QStringLiteral("D") : QStringLiteral("I")) + variant +
         QLatin1Char('|') + FormulaNativeVersion() + QLatin1Char('|') +
         FormulaBackendToken(backend) + QLatin1Char('|') + normalized;
}
