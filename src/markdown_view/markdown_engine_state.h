#ifndef FILE_MARKDOWN_MARKDOWN_ENGINE_STATE_H_
#define FILE_MARKDOWN_MARKDOWN_ENGINE_STATE_H_

#ifdef QLITEMD_HAS_QUICKJS
#include <quickjs.h>
#endif

#include <QColor>
#include <QString>
#include <QVector>

#ifdef QLITEMD_HAS_QUICKJS
#include "js_runtime.h"
#endif
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
#if !defined(QLITEMD_FORMULA_BACKEND_RATEX)
  JsRuntime runtime;
#endif
  // RaTeX 后端无引擎状态：每次调用直接走 FFI
};

struct MermaidEngineState {
#if !defined(QLITEMD_MERMAID_MERMAN)
  // quickjs 后端专用：merman 构建里整个成员与依赖它的函数都不编译，
  // 让"Mermaid 不依赖 QuickJS"在编译期成立
  JsRuntime runtime;
  // initialize 只做一次；主题走每张图的 %%{init}%% 指令
  bool initialized = false;
  // then/catch 落定旗子，泵循环只读它
  bool render_settled = false;
#endif
  // 原生后端；平台带 merman 预编库时启用。QWidget 主路径由核心管线做
  // 运行期切换，这里的旧 adapter 渲染路径只保留原生实现。
  MermanEngine merman;
};

// 只负责加载 runtime，不更新上下文计数；失败是永久性的，除取消后的
// ResetForReload() 外不会重试
#if !defined(QLITEMD_FORMULA_BACKEND_RATEX)
bool EnsureFormulaEngine(FormulaEngineState& engine);
#endif
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
