#ifndef FILE_MARKDOWN_MARKDOWN_ENGINE_STATE_H_
#define FILE_MARKDOWN_MARKDOWN_ENGINE_STATE_H_

#include <quickjs.h>

#include <QColor>
#include <QString>
#include <QVector>

#include "js_runtime.h"
#include "markdown_types.h"

// 内部头文件：直接包含 quickjs.h，消费者需自己拿到 qjs 的头文件路径。
// 一个渲染引擎的运行时状态。同步入口用线程局部默认实例；异步 worker
// 各自持有一份，渲染函数只碰传入的 state，不碰上下文缓存
struct HighlightEngineState {
  JsRuntime runtime;
  JSValue highlight_fn = JS_UNDEFINED;
};

struct FormulaEngineState {
  JsRuntime runtime;
};

struct MermaidEngineState {
  JsRuntime runtime;
  // initialize 只做一次；主题走每张图的 %%{init}%% 指令
  bool initialized = false;
  // then/catch 落定旗子，泵循环只读它
  bool render_settled = false;
};

// 只负责加载 runtime，不更新上下文计数；失败是永久性的，除取消后的
// ResetForReload() 外不会重试
bool EnsureHighlightEngine(HighlightEngineState& engine);
bool EnsureFormulaEngine(FormulaEngineState& engine);
bool EnsureMermaidEngine(MermaidEngineState& engine);

// 返回空串表示失败或取消；不写上下文/磁盘缓存。调度层必须丢弃过期
// request_id 的结果，不能把取消产生的空串当失败结果缓存。
// failed 仅覆盖加载、参数分配、JS 异常；未注册语言返回空串且 failed=false，
// 由调用方回退到纯转义。
QString RenderHighlightOnEngine(HighlightEngineState& engine,
                                const QString& code, const QString& language,
                                const MarkdownDiagnostics& diagnostics,
                                bool* failed);
// 同上：空串 = 失败或取消，返回结果不写缓存
// artifacts 非空时只收集待落盘产物，不直接写文件；空指针保持同步旧行为
QString RenderFormulaOnEngine(
    FormulaEngineState& engine, const QString& tex, bool display,
    const QColor& color, const MarkdownDiagnostics& diagnostics,
    QVector<MarkdownRenderArtifact>* artifacts = nullptr);
QString RenderMermaidOnEngine(
    MermaidEngineState& engine, const QString& code, MarkdownTheme theme,
    const MarkdownDiagnostics& diagnostics,
    QVector<MarkdownRenderArtifact>* artifacts = nullptr);

MarkdownEngineStatus HighlightEngineStatus(const HighlightEngineState& engine);
MarkdownEngineStatus FormulaEngineStatus(const FormulaEngineState& engine);
MarkdownEngineStatus MermaidEngineStatus(const MermaidEngineState& engine);

#endif  // FILE_MARKDOWN_MARKDOWN_ENGINE_STATE_H_
