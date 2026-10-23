#ifndef FILE_MARKDOWN_MARKDOWN_ENGINE_STATE_H_
#define FILE_MARKDOWN_MARKDOWN_ENGINE_STATE_H_

#include <quickjs.h>

#include <QColor>
#include <QString>
#include <QVector>

#include "js_runtime.h"
#include "markdown_types.h"
#include "merman_renderer.h"
#include "tree_sitter_highlighter.h"

// 内部头文件：直接包含 quickjs.h，消费者需自己拿到 qjs 的头文件路径。
// 高亮已是 native tree-sitter（无 JS 运行时）；TSParser 不可跨线程，
// 每个 worker/同步入口各持一份 parser 缓存，渲染函数只碰传入的 state
struct HighlightEngineState {
  TreeSitterParserCache parsers;
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
  // 原生后端；QLITEMD_MERMAID_BACKEND=quickjs 时不加载
  MermanEngine merman;
};

// 只负责加载 runtime，不更新上下文计数；失败是永久性的，除取消后的
// ResetForReload() 外不会重试
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
// 同上：isNull() = 失败或取消，返回结果不写缓存。产物带位图与 PNG，
// GUI 直接 QPixmap::fromImage，缓存/导出直接用 png_bytes
// artifacts 非空时只收集待落盘产物，不直接写文件
RenderedImage RenderFormulaOnEngine(
    FormulaEngineState& engine, const QString& tex, bool display,
    const QColor& color, const MarkdownDiagnostics& diagnostics,
    QVector<MarkdownRenderArtifact>* artifacts = nullptr);
RenderedImage RenderMermaidOnEngine(
    MermaidEngineState& engine, const QString& code, MarkdownTheme theme,
    const MarkdownDiagnostics& diagnostics,
    QVector<MarkdownRenderArtifact>* artifacts = nullptr);

MarkdownEngineStatus HighlightEngineStatus(const HighlightEngineState& engine);
MarkdownEngineStatus FormulaEngineStatus(const FormulaEngineState& engine);
MarkdownEngineStatus MermaidEngineStatus(const MermaidEngineState& engine);

#endif  // FILE_MARKDOWN_MARKDOWN_ENGINE_STATE_H_
