#include "code_highlighter.h"

#include <QElapsedTimer>

#include "html_escape.h"
#include "markdown_engine_state.h"
#include "markdown_render_context_internal.h"
#include "render_diagnostics.h"
#include "tree_sitter_highlighter.h"

namespace {

// 同步入口的线程局部引擎；异步 worker 自己持有 state。
// 高亮已是 native tree-sitter：无 JS 运行时、无载荷加载，state 里只剩
// 按语言惰性创建的 parser 缓存
HighlightEngineState& DefaultEngine() {
  thread_local HighlightEngineState engine;
  return engine;
}

}  // namespace

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
  QElapsedTimer render_clock;
  render_clock.start();

  // 白名单外/解析失败都在内部回退纯转义；native 解析不可中断，
  // 没有需要置 failed 的异步失败面
  QString html = TreeSitterHighlightHtml(code, language, &engine.parsers);

  if (diagnostics.trace) {
    TraceRender(
        diagnostics,
        QStringLiteral("highlight render: %1 ms, %2 bytes, lang=%3")
            .arg(render_clock.elapsed())
            .arg(html.size())
            .arg(language.isEmpty() ? QStringLiteral("text") : language));
  }
  return html;
}

MarkdownEngineStatus HighlightEngineStatus(const HighlightEngineState& engine) {
  // native 高亮无载荷加载阶段：恒就绪（engine 参数仅为签名一致性保留）
  (void)engine;
  return {/*loaded=*/true, /*permanently_failed=*/false, /*first_load_ms=*/0};
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
  // 未注册语言（failed=false）的转义结果可以缓存；native 路径当前没有
  // 会置 failed 的失败面，保留该分支以固化语义
  if (!failed) {
    state.highlight_cache.Insert(key, result);
  }
  return result;
}

QString HighlightCode(const QString& code, const QString& language) {
  return HighlightCode(DefaultRenderContext(), code, language);
}
