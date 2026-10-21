#include "mermaid_renderer.h"

#include <quickjs.h>

#include <atomic>
#include <cmath>

#include <QBuffer>
#include <QDebug>
#include <QElapsedTimer>
#include <QFont>
#include <QFontDatabase>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QHash>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QRegularExpression>
#include <QStringList>
#include <QSvgRenderer>

#include "js_runtime.h"
#include "markdown_bytecode.h"
#include "markdown_engine_state.h"
#include "markdown_render_context_internal.h"
#include "markdown_types.h"
#include "mermaid_svg_cleanup.h"
#include "render_cache.h"
#include "render_diagnostics.h"

namespace {

// 同步入口的线程局部引擎；异步 worker 自己持有 state
MermaidEngineState& DefaultEngine() {
  thread_local MermaidEngineState engine;
  return engine;
}

// 排查渲染问题用：diagnostics.dump_directory 非空（或 MD_MARKDOWN_DUMP /
// MD_MERMAID_DUMP）时把各阶段产物落盘；目录布局见 render_diagnostics.h。
// 文本版本先过 DumpEnabled，避免转储关闭时还构造 toUtf8()
void AddArtifact(const MarkdownDiagnostics& diagnostics, const QString& name,
                 const QByteArray& data,
                 QVector<MarkdownRenderArtifact>* artifacts) {
  if (artifacts != nullptr) {
    artifacts->append(MarkdownRenderArtifact{name, data});
  } else {
    DumpRenderArtifact(diagnostics, name, data);
  }
}

void DumpStageText(const MarkdownDiagnostics& diagnostics, int index,
                   const QString& stage, const QString& text,
                   QVector<MarkdownRenderArtifact>* artifacts) {
  const QString name = QStringLiteral("mermaid/%1-%2").arg(index).arg(stage);
  if (!DumpEnabled(diagnostics, name)) {
    return;
  }
  AddArtifact(diagnostics, name, text.toUtf8(), artifacts);
}

void DumpStageBytes(const MarkdownDiagnostics& diagnostics, int index,
                    const QString& stage, const QByteArray& data,
                    QVector<MarkdownRenderArtifact>* artifacts) {
  const QString name = QStringLiteral("mermaid/%1-%2").arg(index).arg(stage);
  if (!DumpEnabled(diagnostics, name)) {
    return;
  }
  AddArtifact(diagnostics, name, data, artifacts);
}

// ---- 字体 ----
//
// 度量与绘制必须是同一份字体，否则文字会偏移或溢出。做法是选一个平台上有
// CJK 的族名，度量用它回答 mermaid，绘制前再把 SVG 里的 font-family 强制成
// 它（见 InlineSvgCss 的第三个参数）。找不到就返回空串：此时不改写，度量按
// SVG 自己写的族名走——两者都过 Qt 的替换链，仍然同源，只是中文可能缺字。

QString PickCjkFamily() {
  // 只许在 GUI 线程、QGuiApplication 构造之后调：QFontDatabase 需要应用实例。
  // 用运行时检查不用 Q_ASSERT——Release 下断言是空操作，null 会直接炸在
  // families() 里；返回空族，调用方退回 JS 自报的族名，行为可预期
  if (qGuiApp == nullptr) {
    qWarning() << "mermaid: PickCjkFamily called before QGuiApplication";
    return QString();
  }
  const QStringList candidates = {
      QStringLiteral("Microsoft YaHei"),     // Windows
      QStringLiteral("Noto Sans CJK SC"),    // Linux 常见
      QStringLiteral("Source Han Sans SC"),  // 思源黑体
      QStringLiteral("WenQuanYi Micro Hei"), QStringLiteral("SimHei"),
  };
  // families() 在 Qt 5.x 全程都是成员函数（Qt 6 才改成静态），得先建实例
  const QFontDatabase database;
  const QStringList families = database.families();
  for (const QString& name : candidates) {
    if (families.contains(name)) {
      return name;
    }
  }
  qWarning() << "mermaid: no CJK font family found, diagram text may show"
                " as boxes";
  return QString();
}

bool IsBoldWeight(const QString& weight) {
  bool ok = false;
  const int numeric = weight.toInt(&ok);
  if (ok) {
    return numeric >= 600;
  }
  const QString lower = weight.trimmed().toLower();
  return lower == QLatin1String("bold") || lower == QLatin1String("bolder");
}

// mermaid 的度量回调：QFontMetricsF 走的是 QFont 的替换链，与 QSvgRenderer
// 绘制时同一条路。不用 QRawFont——它的 advancesForGlyphs 不做字体替换，
// 缺字时会与绘制对不上。
// 前置条件同 PickCjkFamily：GUI 线程 + QGuiApplication 已构造
QFontMetricsF MetricsFor(const QString& js_family, double size_px,
                         const QString& weight) {
  const QString family = MermaidFontFamily();
  QFont font(family.isEmpty() ? js_family : family);
  font.setPixelSize(qMax(1, qRound(size_px)));
  font.setBold(IsBoldWeight(weight));
  return QFontMetricsF(font);
}

struct TextMeasure {
  double width = 0.0;
  double ascent = 0.0;
  double descent = 0.0;  // 正数
};

TextMeasure MeasureText(const QString& text, double size_px,
                        const QString& js_family, const QString& weight) {
  TextMeasure out;
  if (text.isEmpty() || size_px <= 0.0) {
    return out;
  }
  const QFontMetricsF metrics = MetricsFor(js_family, size_px, weight);
  out.width = metrics.horizontalAdvance(text);
  out.ascent = metrics.ascent();
  out.descent = metrics.descent();
  return out;
}

// ---- QuickJS ----

// then/catch 落定时调一次：置引擎状态里的旗子，不做别的
JSValue JsSetSettledRaw(JSContext* ctx, JSValue, int, JSValue*) {
  auto* engine =
      static_cast<MermaidEngineState*>(JS_GetRuntimeOpaque(JS_GetRuntime(ctx)));
  if (engine != nullptr) {
    engine->render_settled = true;
  }
  return JS_UNDEFINED;
}

JSValue JsLogRaw(JSContext* ctx, JSValue, int argc, JSValue* argv) {
  if (argc >= 1) {
    const char* text = JS_ToCString(ctx, argv[0]);
    if (text != nullptr) {
      qDebug().noquote() << "[mermaid/js]" << text;
      JS_FreeCString(ctx, text);
    }
  }
  return JS_UNDEFINED;
}

JSValue JsMeasureTextRaw(JSContext* ctx, JSValue, int argc, JSValue* argv) {
  if (argc < 5) {
    return JS_NewFloat64(ctx, 0.0);
  }
  const char* text = JS_ToCString(ctx, argv[0]);
  double size = 16.0;
  JS_ToFloat64(ctx, &size, argv[1]);
  // 参数序是 (t, s, f, w, st)：family 在 argv[2]、weight 在 argv[3]。
  // 曾经整体错位一位（family 读了 weight、weight 读了 style），粗体
  // 永远按非粗体度量——长标签会比绘制窄、溢出节点框
  const char* family = JS_ToCString(ctx, argv[2]);
  const char* weight = JS_ToCString(ctx, argv[3]);
  const TextMeasure measure =
      MeasureText(QString::fromUtf8(text != nullptr ? text : ""), size,
                  QString::fromUtf8(family != nullptr ? family : ""),
                  QString::fromUtf8(weight != nullptr ? weight : ""));
  if (text != nullptr) {
    JS_FreeCString(ctx, text);
  }
  if (family != nullptr) {
    JS_FreeCString(ctx, family);
  }
  if (weight != nullptr) {
    JS_FreeCString(ctx, weight);
  }
  return JS_NewFloat64(ctx, measure.width);
}

JSValue JsMeasureTextFullRaw(JSContext* ctx, JSValue, int argc, JSValue* argv) {
  static const char* const kEmpty = "{\"width\":0,\"ascent\":0,\"descent\":0}";
  if (argc < 5) {
    return JS_NewString(ctx, kEmpty);
  }
  const char* text = JS_ToCString(ctx, argv[0]);
  double size = 16.0;
  JS_ToFloat64(ctx, &size, argv[1]);
  // 参数序是 (t, s, f, w, st)：family 在 argv[2]、weight 在 argv[3]。
  // 曾经整体错位一位（family 读了 weight、weight 读了 style），粗体
  // 永远按非粗体度量——长标签会比绘制窄、溢出节点框
  const char* family = JS_ToCString(ctx, argv[2]);
  const char* weight = JS_ToCString(ctx, argv[3]);
  const TextMeasure measure =
      MeasureText(QString::fromUtf8(text != nullptr ? text : ""), size,
                  QString::fromUtf8(family != nullptr ? family : ""),
                  QString::fromUtf8(weight != nullptr ? weight : ""));
  if (text != nullptr) {
    JS_FreeCString(ctx, text);
  }
  if (family != nullptr) {
    JS_FreeCString(ctx, family);
  }
  if (weight != nullptr) {
    JS_FreeCString(ctx, weight);
  }

  QJsonObject object;
  object[QStringLiteral("width")] = measure.width;
  object[QStringLiteral("ascent")] = measure.ascent;
  object[QStringLiteral("descent")] = measure.descent;
  const QByteArray json = QJsonDocument(object).toJson(QJsonDocument::Compact);
  return JS_NewStringLen(ctx, json.constData(), json.size());
}

bool EvalGlobal(MermaidEngineState& engine, const QByteArray& code,
                const char* name) {
  return engine.runtime.Eval(
      code, name, QStringLiteral("mermaid: eval %1").arg(QLatin1String(name)));
}

bool EnsureLoaded(MermaidEngineState& engine) {
  if (engine.runtime.status().loaded) {
    return true;
  }
  if (engine.runtime.status().permanently_failed) {
    return false;
  }
  // C 回调通过 runtime opaque 找回引擎状态；initialize 失败时复位标志
  engine.runtime.SetRuntimeOpaque(&engine);
  engine.runtime.set_on_fail(
      [&engine](JSContext*) { engine.initialized = false; });
  engine.runtime.set_preserve_first_load(true);
  return engine.runtime.EnsureLoaded(
      {JsPayload{md_pb, md_pb_size, "path_bbox.js"},
       JsPayload{md_ds, md_ds_size, "dom_shim.js"},
       JsPayload{md_mm, md_mm_size, "mermaid.js"}},
      [&engine](JSContext* context) {
        // dom_shim 期望的函数与 bridge 必须在载荷之前就位
        JSValue global = JS_GetGlobalObject(context);
        JS_SetPropertyStr(context, global, "__log_raw",
                          JS_NewCFunction(context, JsLogRaw, "__log_raw", 1));
        JS_SetPropertyStr(
            context, global, "__measureText_raw",
            JS_NewCFunction(context, JsMeasureTextRaw, "__measureText_raw", 5));
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
        return EvalGlobal(engine, QByteArray(kBridge), "bridge");
      },
      [&engine](JSContext*) {
        // mermaid.js 是 esbuild 打的 ESM，手动把 default 暴露出来
        static const char* const kExpose =
            "globalThis.mermaid = "
            "(globalThis.__esbuild_esm_mermaid_nm.mermaid.default"
            " || globalThis.__esbuild_esm_mermaid_nm.mermaid);";
        return EvalGlobal(engine, QByteArray(kExpose), "expose");
      });
}

const char* ThemeName(MarkdownTheme theme) {
  switch (theme) {
    case MarkdownTheme::Light:
      return "default";
    case MarkdownTheme::Dark:
      return "dark";
  }
  return "default";
}

// per-diagram 主题指令：mermaid 公开的 render API 没有 per-call config，
// 但 %%{init}%% 能覆盖 initialize 的主题，且不泄漏到下一次渲染（实测）。
// 这样 initialize 不必随视图主题反复重设，多视图也不会互相污染
QString ThemeDirective(MarkdownTheme theme) {
  if (theme == MarkdownTheme::Light) {
    return {};
  }
  // 闭合 config 对象与 %%{init}%% 指令本身
  return QStringLiteral("%%{init: {'theme': '%1'}}%%\n")
      .arg(QLatin1String(ThemeName(theme)));
}

// 把主题指令插进源码。YAML frontmatter 必须位于文档最开头（mermaid 用
// 行首锚点识别），有 frontmatter 时指令要插到它的闭合分隔线之后
QString ApplyThemeDirective(const QString& code, MarkdownTheme theme) {
  const QString directive = ThemeDirective(theme);
  if (directive.isEmpty()) {
    return code;
  }
  const int first_line_end = code.indexOf(QLatin1Char('\n'));
  if (first_line_end > 0 &&
      code.left(first_line_end).trimmed() == QLatin1String("---")) {
    // 闭合分隔线必须单独成行：`---foo` 不是 frontmatter 结尾，不能据此
    // 插指令，否则渲染失败会被误归因到主题指令
    int line_start = first_line_end + 1;
    while (line_start <= code.size()) {
      const int line_end = code.indexOf(QLatin1Char('\n'), line_start);
      const int end = line_end < 0 ? code.size() : line_end;
      if (code.mid(line_start, end - line_start).trimmed() ==
          QLatin1String("---")) {
        if (line_end < 0) {
          return code + QLatin1Char('\n') + directive;
        }
        const int insert_at = line_end + 1;
        return code.left(insert_at) + directive + code.mid(insert_at);
      }
      if (line_end < 0) {
        break;
      }
      line_start = line_end + 1;
    }
  }
  return directive + code;
}

// htmlLabels:false 是必填项：默认会把节点文字放进 <foreignObject>，而
// QSvgRenderer 完全不认，文字会全部消失。initialize 是整体替换、不是增量，
// 所以配置一次给全；主题不在这里，由 ThemeDirective 逐图指定
bool EnsureInitialized(MermaidEngineState& engine) {
  if (engine.initialized) {
    return true;
  }
  const QString init = QStringLiteral(
      "mermaid.initialize({"
      "  startOnLoad: false,"
      "  htmlLabels: false,"
      "  flowchart: { htmlLabels: false },"
      "  class: { htmlLabels: false },"
      "  state: { htmlLabels: false },"
      "  er: { useMaxWidth: true }"
      "});");
  if (!EvalGlobal(engine, init.toUtf8(), "init")) {
    // initialize 失败（配置字段变化、OOM 等）不能每次渲染都重试：
    // 与载荷加载失败同一套一次性语义，直接作废引擎
    engine.runtime.Abort();
    return false;
  }
  engine.initialized = true;
  return true;
}

// 渲染：mermaid.render() 返回 Promise，而 QuickJS 的微任务不会自动跑，必须
// 手动泵（dom_shim 把 rAF/setTimeout 换成了微任务，所以循环能收敛）
QString RenderMermaidSvg(MermaidEngineState& engine, const QString& code,
                         const MarkdownDiagnostics& diagnostics) {
  if (!EnsureLoaded(engine)) {
    if (diagnostics.trace) {
      TraceRender(diagnostics, QStringLiteral("mermaid render: not loaded"));
    }
    return QString();
  }
  if (!EvalGlobal(engine, QByteArray("__resetDocument();"), "reset")) {
    if (diagnostics.trace) {
      TraceRender(diagnostics, QStringLiteral("mermaid render: reset failed"));
    }
    return QString();
  }

  const QByteArray code_utf8 = code.toUtf8();
  JSValue global = JS_GetGlobalObject(engine.runtime.context());
  JS_SetPropertyStr(engine.runtime.context(), global, "__code",
                    JS_NewStringLen(engine.runtime.context(),
                                    code_utf8.constData(), code_utf8.size()));
  JS_SetPropertyStr(engine.runtime.context(), global, "__renderId",
                    JS_NewString(engine.runtime.context(), "m0"));
  JS_FreeValue(engine.runtime.context(), global);

  // 完成判据是 C 侧的旗子：then 与 catch 都必须调 __setSettled_raw，否则
  // 出错的一路只能等泵循环跑满预算才放弃。JS 侧不再维护 __renderSettled
  static const char* const kCall =
      "globalThis.__renderResult = null;\n"
      "globalThis.__renderError = null;\n"
      "mermaid.render(__renderId, __code)\n"
      "  .then(r => { globalThis.__renderResult = r.svg || '';\n"
      "               __setSettled_raw(); })\n"
      "  .catch(e => { globalThis.__renderError = "
      "      (e && e.name ? e.name + ': ' + e.message : String(e));\n"
      "                 __setSettled_raw(); });";
  engine.render_settled = false;
  if (!EvalGlobal(engine, QByteArray(kCall), "render-call")) {
    if (diagnostics.trace) {
      TraceRender(diagnostics,
                  QStringLiteral("mermaid render: call eval failed"));
    }
    return QString();
  }

  // 上界与 mermaidx 同值：mindmap 这类图会不停排 rAF，靠它兜底。
  // 光有 job 数不够——"小步慢排"的图永远到不了上限但 UI 照样卡死，
  // 加墙钟 30 秒兜底
  constexpr int kMaxJobs = 200000;
  constexpr qint64 kPumpDeadlineMs = 30000;
  QElapsedTimer pump_clock;
  pump_clock.start();
  bool done = false;
  for (int i = 0; i < kMaxJobs; ++i) {
    JSContext* pump_context = nullptr;
    const int result =
        JS_ExecutePendingJob(engine.runtime.runtime(), &pump_context);
    if (result < 0) {
      JSValue exception = JS_GetException(engine.runtime.context());
      const char* raw_message =
          JS_ToCString(engine.runtime.context(), exception);
      const QString message =
          QString::fromUtf8(raw_message != nullptr ? raw_message : "?");
      qWarning() << "mermaid: pump job exception:" << message;
      if (raw_message != nullptr) {
        JS_FreeCString(engine.runtime.context(), raw_message);
      }
      JS_FreeValue(engine.runtime.context(), exception);
      if (diagnostics.trace) {
        TraceRender(diagnostics,
                    QStringLiteral("mermaid pump exception: %1").arg(message));
      }
      break;
    }
    if (result == 0) {
      break;
    }
    if (pump_clock.elapsed() > kPumpDeadlineMs) {
      qWarning() << "mermaid: pump exceeded" << kPumpDeadlineMs << "ms";
      if (diagnostics.trace) {
        TraceRender(
            diagnostics,
            QStringLiteral("mermaid pump exceeded %1 ms").arg(kPumpDeadlineMs));
      }
      break;
    }
    // JS 的 then/catch 调 __setSettled_raw 置的是一面 C 侧旗子，读它
    // 不进 JS 引擎——最坏二十万次迭代也就是二十万次 bool 读
    if (engine.render_settled) {
      done = true;
      break;
    }
  }
  if (!done) {
    qWarning() << "mermaid: render did not settle within job budget";
    if (diagnostics.trace) {
      TraceRender(diagnostics,
                  QStringLiteral("mermaid render: did not settle in budget"));
    }
    return QString();
  }

  global = JS_GetGlobalObject(engine.runtime.context());
  JSValue error =
      JS_GetPropertyStr(engine.runtime.context(), global, "__renderError");
  if (!JS_IsNull(error) && !JS_IsUndefined(error)) {
    const char* raw_message = JS_ToCString(engine.runtime.context(), error);
    const QString message =
        QString::fromUtf8(raw_message != nullptr ? raw_message : "?");
    qWarning() << "mermaid render error:" << message;
    if (raw_message != nullptr) {
      JS_FreeCString(engine.runtime.context(), raw_message);
    }
    JS_FreeValue(engine.runtime.context(), error);
    JS_FreeValue(engine.runtime.context(), global);
    if (diagnostics.trace) {
      TraceRender(diagnostics,
                  QStringLiteral("mermaid render error: %1").arg(message));
    }
    return QString();
  }
  JS_FreeValue(engine.runtime.context(), error);

  QString svg;
  JSValue value =
      JS_GetPropertyStr(engine.runtime.context(), global, "__renderResult");
  if (!JS_IsNull(value) && !JS_IsUndefined(value)) {
    const char* text = JS_ToCString(engine.runtime.context(), value);
    svg = QString::fromUtf8(text != nullptr ? text : "");
    if (text != nullptr) {
      JS_FreeCString(engine.runtime.context(), text);
    }
  }
  JS_FreeValue(engine.runtime.context(), value);
  JS_FreeValue(engine.runtime.context(), global);
  if (svg.isEmpty() && diagnostics.trace) {
    TraceRender(diagnostics, QStringLiteral("mermaid render: empty result"));
  }
  return svg;
}

// 后处理三层，顺序不可颠倒：CleanSvg（文本定标）→ InlineSvgCss（CSS 内联 +
// 字体族强制）→ InlineSvgMarkers（marker 展开，颜色来自上一步内联的规则）
QString SvgToImgTag(const QString& svg, const MarkdownDiagnostics& diagnostics,
                    QVector<MarkdownRenderArtifact>* artifacts) {
  // worker 并发时用原子序号，避免产物同名互相覆盖
  static std::atomic<int> dump_index{0};
  // 与 formula 的索引对齐：都从 1 开始
  const int index = dump_index.fetch_add(1, std::memory_order_relaxed) + 1;

  // mermaid 的容器里可能还有别的，只留 <svg>...</svg>
  const int start = svg.indexOf(QLatin1String("<svg"));
  const int end = svg.lastIndexOf(QLatin1String("</svg>"));
  if (start < 0 || end < 0) {
    if (diagnostics.trace) {
      TraceRender(diagnostics, QStringLiteral("mermaid svg: no <svg> root"));
    }
    return QString();
  }
  const QString bare = svg.mid(start, end - start + 6);

  const QString cleaned = CleanSvg(bare);
  const QString inlined = InlineSvgCss(cleaned, MermaidFontFamily());
  const QString marked = InlineSvgMarkers(inlined);
  DumpStageText(diagnostics, index, QStringLiteral("raw.svg"), bare, artifacts);
  DumpStageText(diagnostics, index, QStringLiteral("clean.svg"), cleaned,
                artifacts);
  DumpStageText(diagnostics, index, QStringLiteral("inlined.svg"), inlined,
                artifacts);
  DumpStageText(diagnostics, index, QStringLiteral("marked.svg"), marked,
                artifacts);
  if (marked.isEmpty()) {
    if (diagnostics.trace) {
      TraceRender(diagnostics,
                  QStringLiteral("mermaid svg: markers/css inlining empty"));
    }
    return QString();
  }

  // 尺寸从 viewBox 读：mermaid 的根元素写的是 width="100%"
  static const QRegularExpression kViewBox(
      QStringLiteral("viewBox=\"([-\\d.]+) ([-\\d.]+) ([\\d.]+) ([\\d.]+)\""));
  const QRegularExpressionMatch match = kViewBox.match(marked);
  qreal width = 0.0;
  qreal height = 0.0;
  if (match.hasMatch()) {
    width = match.captured(3).toDouble();
    height = match.captured(4).toDouble();
  }
  if (width < 1.0 || height < 1.0) {
    // 尺寸兜底必须量清理后的 SVG：bare 里还有 <style>/<marker>，QSvgRenderer
    // 认不了这些节点（"Could not add child element"），defaultSize 未必准
    QSvgRenderer probe(marked.toUtf8());
    const QSize size = probe.defaultSize();
    width = size.width();
    height = size.height();
  }
  if (width < 1.0 || height < 1.0) {
    if (diagnostics.trace) {
      TraceRender(
          diagnostics,
          QStringLiteral("mermaid svg: bad size %1x%2").arg(width).arg(height));
    }
    return QString();
  }

  // 2 倍超采样、按 1 倍尺寸摆放：连线与箭头的边缘才是锐的。
  // Format_ARGB32 是 4 字节/像素，大图 2 倍会内存爆（5000×5000 的图 2 倍
  // = 1.6GB）：像素总量封顶 4096×4096（≈67MB 一张），超了自动降倍数
  QSvgRenderer renderer(marked.toUtf8());
  if (!renderer.isValid()) {
    qWarning() << "mermaid: QSvgRenderer rejected the cleaned svg";
    if (diagnostics.trace) {
      TraceRender(diagnostics,
                  QStringLiteral("mermaid svg: QSvgRenderer rejected it"));
    }
    return QString();
  }
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
      TraceRender(diagnostics, QStringLiteral("mermaid png: save failed"));
    }
    return QString();  // PNG 编不出：返回空，原图位退回代码块
  }
  DumpStageBytes(diagnostics, index, QStringLiteral("png"), buffer.data(),
                 artifacts);

  return QStringLiteral(
             "<img class=\"mermaid\" src=\"data:image/png;base64,%1\" "
             "width=\"%2\" height=\"%3\">")
      .arg(QString::fromLatin1(buffer.data().toBase64()))
      .arg(qRound(width))
      .arg(qRound(height));
}

// 字体指纹：族名 + 16px 下的关键度量。字体更新或 fallback 变化都会改变
// 指纹，缓存 key 因此失效；不依赖拿得到字体文件路径
QString FontFingerprint() {
  // 无 GUI 应用时 QFontMetricsF 不可用；任务收集阶段可能只算 key，
  // 此时不缓存占位，等应用对象出现后再算真实指纹
  if (qGuiApp == nullptr) {
    return QStringLiteral("no-gui");
  }
  static const QString fingerprint = [] {
    const QString family = MermaidFontFamily();
    QFont font(family.isEmpty() ? QStringLiteral("sans-serif") : family);
    font.setPixelSize(16);
    const QFontMetricsF metrics(font);
    return QStringLiteral("%1:%2:%3:%4")
        .arg(family)
        .arg(qRound(metrics.ascent() * 100.0))
        .arg(qRound(metrics.descent() * 100.0))
        .arg(qRound(metrics.horizontalAdvance(QStringLiteral("中A")) * 100.0));
  }();
  return fingerprint;
}

}  // namespace

bool EnsureMermaidEngine(MermaidEngineState& engine) {
  return EnsureLoaded(engine);
}

MarkdownEngineStatus MermaidEngineStatus(const MermaidEngineState& engine) {
  return engine.runtime.status();
}

MarkdownEngineStatus MermaidEngineStatus() {
  return MermaidEngineStatus(DefaultEngine());
}

QString RenderMermaidOnEngine(MermaidEngineState& engine, const QString& code,
                              MarkdownTheme theme,
                              const MarkdownDiagnostics& diagnostics,
                              QVector<MarkdownRenderArtifact>* artifacts) {
  const bool was_loaded = engine.runtime.status().loaded;
  if (!EnsureLoaded(engine) || !EnsureInitialized(engine)) {
    if (diagnostics.trace) {
      TraceRender(diagnostics, QStringLiteral("mermaid load/init failed"));
    }
    return {};
  }
  if (!was_loaded && diagnostics.trace) {
    // 冷加载单独记账：first_load_ms 已经算好，别混进下面的渲染耗时
    TraceRender(diagnostics, QStringLiteral("mermaid engine loaded in %1 ms")
                                 .arg(engine.runtime.status().first_load_ms));
  }
  QElapsedTimer render_clock;
  render_clock.start();
  const QString svg =
      RenderMermaidSvg(engine, ApplyThemeDirective(code, theme), diagnostics);
  const QString tag =
      svg.isEmpty() ? QString() : SvgToImgTag(svg, diagnostics, artifacts);
  if (diagnostics.trace) {
    TraceRender(diagnostics, QStringLiteral("mermaid render: %1 ms, %2 bytes")
                                 .arg(render_clock.elapsed())
                                 .arg(tag.size()));
  }
  return tag;
}

bool MermaidRendererReady() { return EnsureMermaidEngine(DefaultEngine()); }

void ClearMermaidCache() {
  MarkdownRenderContextAccess::State(DefaultRenderContext())
      .mermaid_cache.Clear();
}

QString MermaidFontFamily() {
  static const QString family = PickCjkFamily();
  return family;
}

QString MermaidRenderVariant(MarkdownTheme theme) {
  // 主题名进 key；字体指纹让字体更新后的旧图自然失效
  return QString::fromLatin1(ThemeName(theme)) + QLatin1Char('|') +
         FontFingerprint();
}

QString MermaidRenderKey(const QString& code, MarkdownTheme theme) {
  return MermaidRenderVariant(theme) + QLatin1Char('|') + code;
}

QString MermaidToImgTag(MarkdownRenderContext& context, const QString& code,
                        MarkdownTheme theme, QString* cache_key) {
  MarkdownRenderContextState& state =
      MarkdownRenderContextAccess::State(context);
  const QString variant = QString::fromLatin1(ThemeName(theme)) +
                          QLatin1Char('|') + FontFingerprint();
  const QString key = MermaidRenderKey(code, theme);
  if (cache_key != nullptr) {
    *cache_key = key;
  }
  const MarkdownDiagnostics& diagnostics = state.options.diagnostics;
  if (const QString* cached = state.mermaid_cache.Find(key)) {
    ++state.mermaid_stats.memory_cache_hits;
    if (diagnostics.trace) {
      TraceRender(diagnostics,
                  QStringLiteral("mermaid cache hit: %1").arg(code.left(60)));
    }
    return *cached;
  }
  ++state.mermaid_stats.memory_cache_misses;
  const RenderCacheConfig cache_config =
      MakeRenderCacheConfig(state.options, QStringLiteral("mermaid"));
  QString disk_key;
  if (cache_config.enabled) {
    disk_key = MakeRenderCacheKey(QStringLiteral("mermaid"), code, variant);
    QString disk_tag;
    if (LoadRenderCacheEntry(cache_config, QStringLiteral("mermaid"), disk_key,
                             &disk_tag)) {
      ++state.mermaid_stats.disk_cache_hits;
      if (diagnostics.trace) {
        TraceRender(diagnostics,
                    QStringLiteral("mermaid disk hit: %1").arg(code.left(60)));
      }
      state.mermaid_cache.Insert(key, disk_tag);
      return disk_tag;
    }
    ++state.mermaid_stats.disk_cache_misses;
  }
  ++state.mermaid_stats.renders;
  const QString tag =
      RenderMermaidOnEngine(DefaultEngine(), code, theme, diagnostics);
  if (tag.isEmpty()) {
    ++state.mermaid_stats.failures;
  }
  state.mermaid_cache.Insert(key, tag);
  if (!tag.isEmpty() && !disk_key.isEmpty()) {
    StoreRenderCacheEntry(cache_config, QStringLiteral("mermaid"), disk_key,
                          tag);
  }
  return tag;
}

QString MermaidToImgTag(const QString& code) {
  return MermaidToImgTag(DefaultRenderContext(), code, MarkdownTheme::Light);
}
