#include "formula_renderer.h"

#include <quickjs.h>

#include <atomic>
#include <cmath>

#include <QBuffer>
#include <QDebug>
#include <QElapsedTimer>
#include <QImage>
#include <QPainter>
#include <QRegularExpression>
#include <QSvgRenderer>

#include "js_runtime.h"
#include "markdown_bytecode.h"
#include "markdown_engine_state.h"
#include "markdown_render_context_internal.h"
#include "render_cache.h"
#include "render_diagnostics.h"

namespace {

// 同步入口的线程局部引擎；异步 worker 自己持有 state
FormulaEngineState& DefaultEngine() {
  thread_local FormulaEngineState engine;
  return engine;
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

// SVG 字符串 → <img>。失败返回空串。color 有效时替换 currentColor
QString SvgToImgTag(const QString& svg, bool display, const QColor& color,
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

  QSvgRenderer renderer(bare.toUtf8());
  if (!renderer.isValid()) {
    if (diagnostics.trace) {
      TraceRender(diagnostics,
                  QStringLiteral("formula svg: QSvgRenderer rejected it"));
    }
    return {};
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
    const QSize fallback = renderer.defaultSize();
    width = fallback.width() / 1000.0 * 16.0;
    height = fallback.height() / 1000.0 * 16.0;
  }
  if (width < 1.0 || height < 1.0) {
    if (diagnostics.trace) {
      TraceRender(
          diagnostics,
          QStringLiteral("formula svg: bad size %1x%2").arg(width).arg(height));
    }
    return {};
  }

  // 2 倍超采样、按 1 倍尺寸摆放：公式里的分数线与根号才有锐边。
  // 像素总量与 mermaid 同口径封顶，防止超大公式把内存打爆
  qreal scale = 2.0;
  constexpr qreal kMaxRasterPixels = 4096.0 * 4096.0;
  const qreal area = width * height;
  if (area * scale * scale > kMaxRasterPixels) {
    scale = std::sqrt(kMaxRasterPixels / area);
  }
  QImage image(qRound(width * scale), qRound(height * scale),
               QImage::Format_ARGB32_Premultiplied);
  image.fill(Qt::transparent);
  QPainter painter(&image);
  renderer.render(&painter, QRectF(0.0, 0.0, width * scale, height * scale));
  painter.end();

  QBuffer buffer;
  buffer.open(QIODevice::WriteOnly);
  if (!image.save(&buffer, "PNG")) {
    if (diagnostics.trace) {
      TraceRender(diagnostics, QStringLiteral("formula png: save failed"));
    }
    return {};  // PNG 编不出：返回空，正文落回原文而不是坏 img
  }
  if (DumpEnabled(diagnostics, artifact_prefix)) {
    AddArtifact(diagnostics, artifact_prefix + QStringLiteral(".png"),
                buffer.data(), artifacts);
  }

  // data URI 内联：自定义 scheme 会被 qlitehtml 当主机名做浏览器式扩展
  // （实测 mj://0 变成 mj://0.0.0.0），走 data URI 完全不经 URL 解析
  return QStringLiteral(
             "<img%1 src=\"data:image/png;base64,%2\" width=\"%3\" "
             "height=\"%4\">")
      .arg(display ? QStringLiteral(" class=\"math-display\"") : QString(),
           QString::fromLatin1(buffer.data().toBase64()))
      .arg(qRound(width))
      .arg(qRound(height));
}

}  // namespace

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

QString RenderFormulaOnEngine(FormulaEngineState& engine, const QString& tex,
                              bool display, const QColor& color,
                              const MarkdownDiagnostics& diagnostics,
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

  QString tag;
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
        tag =
            SvgToImgTag(svg, display, color, diagnostics, artifact, artifacts);
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
                    .arg(tag.size())
                    .arg(display));
  }
  return tag;
}

bool FormulaRendererReady() { return EnsureFormulaEngine(DefaultEngine()); }

void ClearFormulaCache() {
  MarkdownRenderContextAccess::State(DefaultRenderContext())
      .formula_cache.Clear();
}

QString FormulaRenderKey(const QString& tex, bool display,
                         const QColor& color) {
  // MathJax 在数学模式下对首尾空白不敏感：key 与渲染输入统一用 trim 后的
  // 文本，避免 "x" 与 " x " 生成两条内容相同的缓存
  const QString normalized = tex.trimmed();
  const QString variant =
      color.isValid() ? color.name() : QStringLiteral("legacy");
  return (display ? QStringLiteral("D") : QStringLiteral("I")) + variant +
         QLatin1Char('|') + normalized;
}

QString FormulaToImgTag(MarkdownRenderContext& context, const QString& tex,
                        bool display, const QColor& color, QString* cache_key) {
  MarkdownRenderContextState& state =
      MarkdownRenderContextAccess::State(context);
  const QString normalized = tex.trimmed();
  const QString variant =
      color.isValid() ? color.name() : QStringLiteral("legacy");
  const QString key = FormulaRenderKey(normalized, display, color);
  if (cache_key != nullptr) {
    *cache_key = key;
  }
  const MarkdownDiagnostics& diagnostics = state.options.diagnostics;
  if (const QString* cached = state.formula_cache.Find(key)) {
    ++state.formula_stats.memory_cache_hits;
    if (diagnostics.trace) {
      TraceRender(
          diagnostics,
          QStringLiteral("formula cache hit: %1").arg(normalized.left(60)));
    }
    return *cached;
  }
  ++state.formula_stats.memory_cache_misses;
  const RenderCacheConfig cache_config =
      MakeRenderCacheConfig(state.options, QStringLiteral("formula"));
  QString disk_key;
  if (cache_config.enabled) {
    disk_key = MakeRenderCacheKey(
        QStringLiteral("formula"), normalized,
        (display ? QStringLiteral("D") : QStringLiteral("I")) +
            QLatin1Char('|') + variant);
    QString disk_tag;
    if (LoadRenderCacheEntry(cache_config, QStringLiteral("formula"), disk_key,
                             &disk_tag)) {
      ++state.formula_stats.disk_cache_hits;
      if (diagnostics.trace) {
        TraceRender(
            diagnostics,
            QStringLiteral("formula disk hit: %1").arg(normalized.left(60)));
      }
      state.formula_cache.Insert(key, disk_tag);
      return disk_tag;
    }
    ++state.formula_stats.disk_cache_misses;
  }
  ++state.formula_stats.renders;
  const QString tag = RenderFormulaOnEngine(DefaultEngine(), normalized,
                                            display, color, diagnostics);
  if (tag.isEmpty()) {
    ++state.formula_stats.failures;
  }
  state.formula_cache.Insert(key, tag);
  if (!tag.isEmpty() && !disk_key.isEmpty()) {
    StoreRenderCacheEntry(cache_config, QStringLiteral("formula"), disk_key,
                          tag);
  }
  return tag;
}

QString FormulaToImgTag(MarkdownRenderContext& context, const QString& tex,
                        bool display) {
  return FormulaToImgTag(context, tex, display, QColor());
}

QString FormulaToImgTag(const QString& tex, bool display) {
  return FormulaToImgTag(DefaultRenderContext(), tex, display);
}
