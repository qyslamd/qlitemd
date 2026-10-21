#include "code_highlighter.h"

#include <quickjs.h>

#include <QDebug>
#include <QElapsedTimer>

#include "html_escape.h"
#include "markdown_bytecode.h"
#include "markdown_engine_state.h"
#include "markdown_render_context_internal.h"
#include "render_diagnostics.h"

namespace {

// 同步入口的线程局部引擎；异步 worker 自己持有 state
HighlightEngineState& DefaultEngine() {
  thread_local HighlightEngineState engine;
  return engine;
}

void ReleaseHighlightFn(JSContext* context, JSValue* fn) {
  JS_FreeValue(context, *fn);
  *fn = JS_UNDEFINED;
}

}  // namespace

bool EnsureHighlightEngine(HighlightEngineState& engine) {
  if (engine.runtime.status().loaded) {
    return true;
  }
  if (engine.runtime.status().permanently_failed) {
    return false;
  }
  engine.runtime.set_on_fail([&engine](JSContext* context) {
    ReleaseHighlightFn(context, &engine.highlight_fn);
  });
  return engine.runtime.EnsureLoaded(
      {JsPayload{md_hl, md_hl_size, "highlight.min.js"}}, nullptr,
      [&engine](JSContext* context) {
        // 只解析一次函数引用；载荷没导出 __highlight 说明资源版本对不上
        JSValue global = JS_GetGlobalObject(context);
        engine.highlight_fn = JS_GetPropertyStr(context, global, "__highlight");
        JS_FreeValue(context, global);
        if (!JS_IsFunction(context, engine.highlight_fn)) {
          qWarning() << "highlight payload lacks __highlight";
          return false;
        }
        return true;
      });
}

QString RenderHighlightOnEngine(HighlightEngineState& engine,
                                const QString& code, const QString& language,
                                const MarkdownDiagnostics& diagnostics,
                                bool* failed) {
  if (failed != nullptr) {
    *failed = false;
  }
  if (code.isEmpty()) {
    return {};
  }
  const bool was_loaded = engine.runtime.status().loaded;
  if (!EnsureHighlightEngine(engine)) {
    if (failed != nullptr) {
      *failed = true;
    }
    if (diagnostics.trace) {
      TraceRender(diagnostics, QStringLiteral("highlight load failed"));
    }
    return {};
  }
  if (!was_loaded && diagnostics.trace) {
    // 冷加载单独记账：first_load_ms 已经算好，别混进下面的渲染耗时
    TraceRender(diagnostics, QStringLiteral("highlight engine loaded in %1 ms")
                                 .arg(engine.runtime.status().first_load_ms));
  }
  JSContext* js = engine.runtime.context();
  QElapsedTimer render_clock;
  render_clock.start();

  // 参数直接以 JS 字符串传入：不用手写 JS 源、不用全局变量，也不牵扯转义
  const QByteArray code_utf8 = code.toUtf8();
  const QByteArray lang_utf8 = language.toUtf8();
  JSValue argv[2] = {
      JS_NewStringLen(js, code_utf8.constData(), code_utf8.size()),
      JS_NewStringLen(js, lang_utf8.constData(), lang_utf8.size()),
  };
  if (JS_IsException(argv[0]) || JS_IsException(argv[1])) {
    // 分配失败返回的是异常哨兵，不能当参数传给 JS_Call；取出并清掉
    // pending 异常，退回纯转义
    const QString message = JsRuntime::TakeExceptionMessage(js);
    qWarning() << "highlight argument alloc failed:" << message;
    JS_FreeValue(js, argv[0]);
    JS_FreeValue(js, argv[1]);
    if (failed != nullptr) {
      *failed = true;
    }
    if (diagnostics.trace) {
      TraceRender(diagnostics,
                  QStringLiteral("highlight argument alloc failed"));
    }
    return {};
  }
  const JSValue value = JS_Call(js, engine.highlight_fn, JS_UNDEFINED, 2, argv);
  JS_FreeValue(js, argv[0]);
  JS_FreeValue(js, argv[1]);

  QString result;
  QString error;
  if (!JsRuntime::CheckResult(js, value, QStringLiteral("highlight call"),
                              &error)) {
    if (failed != nullptr) {
      *failed = true;
    }
    if (diagnostics.trace) {
      TraceRender(diagnostics,
                  QStringLiteral("highlight exception: %1").arg(error));
    }
  } else if (JS_IsNull(value) || JS_IsUndefined(value)) {
    // __highlight 对未注册语言返回 null；JS_ToCString 会把它变成字面 "null"
  } else {
    const char* text = JS_ToCString(js, value);
    result = QString::fromUtf8(text != nullptr ? text : "");
    if (text != nullptr) {
      JS_FreeCString(js, text);
    }
  }
  JS_FreeValue(js, value);

  // 空结果（未注册语言、JS 侧异常）由调用方退纯转义；未注册语言是正常
  // 回退，不计 failure
  if (result.isEmpty()) {
    if (diagnostics.trace) {
      TraceRender(
          diagnostics,
          QStringLiteral("highlight fallback: language=%1")
              .arg(language.isEmpty() ? QStringLiteral("text") : language));
    }
    return {};
  }
  if (diagnostics.trace) {
    TraceRender(diagnostics,
                QStringLiteral("highlight %1: %2 ms, %3 bytes")
                    .arg(language.isEmpty() ? QStringLiteral("text") : language)
                    .arg(render_clock.elapsed())
                    .arg(result.size()));
  }
  return result;
}

MarkdownEngineStatus HighlightEngineStatus(const HighlightEngineState& engine) {
  return engine.runtime.status();
}

MarkdownEngineStatus HighlightEngineStatus() {
  return HighlightEngineStatus(DefaultEngine());
}

QString HighlightRenderKey(const QString& code, const QString& language) {
  return language + QLatin1Char('\n') + code;
}

QString HighlightCode(MarkdownRenderContext& context, const QString& code,
                      const QString& language) {
  if (code.isEmpty()) {
    return {};
  }
  MarkdownRenderContextState& state =
      MarkdownRenderContextAccess::State(context);
  const MarkdownDiagnostics& diagnostics = state.options.diagnostics;
  const QString key = HighlightRenderKey(code, language);
  if (const QString* cached = state.highlight_cache.Find(key)) {
    ++state.highlight_stats.memory_cache_hits;
    if (diagnostics.trace) {
      TraceRender(
          diagnostics,
          QStringLiteral("highlight cache hit: language=%1")
              .arg(language.isEmpty() ? QStringLiteral("text") : language));
    }
    return *cached;
  }
  ++state.highlight_stats.memory_cache_misses;
  ++state.highlight_stats.renders;

  bool failed = false;
  QString result = RenderHighlightOnEngine(DefaultEngine(), code, language,
                                           diagnostics, &failed);
  if (failed) {
    ++state.highlight_stats.failures;
  }
  if (result.isEmpty()) {
    result = EscapeHtml(code);
  }
  // 未注册语言（failed=false）的转义结果可以缓存；加载/JS 异常不缓存，
  // 下次还有重试机会
  if (!failed) {
    state.highlight_cache.Insert(key, result);
  }
  return result;
}

QString HighlightCode(const QString& code, const QString& language) {
  return HighlightCode(DefaultRenderContext(), code, language);
}
