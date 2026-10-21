#ifndef FILE_MARKDOWN_MARKDOWN_RENDER_CONTEXT_H_
#define FILE_MARKDOWN_MARKDOWN_RENDER_CONTEXT_H_

#include <memory>

#include "markdown_types.h"

// 每个 MarkdownView 一份的渲染状态：内存缓存、计数、配置。
// 引擎状态不在这里：同步入口使用线程局部默认引擎，异步 worker 各自持有；
// 上下文本体限定单线程使用（当前 GUI 线程），不可拷贝。
class MarkdownRenderContext {
 public:
  MarkdownRenderContext();
  ~MarkdownRenderContext();
  MarkdownRenderContext(const MarkdownRenderContext&) = delete;
  MarkdownRenderContext& operator=(const MarkdownRenderContext&) = delete;

  void SetOptions(const MarkdownRenderContextOptions& options);
  const MarkdownRenderContextOptions& options() const;
  void SetDiagnostics(const MarkdownDiagnostics& diagnostics);
  MarkdownDiagnostics diagnostics() const;
  // 本上下文计数 + 调用线程的默认引擎状态；不触发任何加载
  MarkdownRendererStatus status() const;
  // 当前内存缓存的实际条目与估算占用；供状态面板诊断
  MarkdownCacheUsage cache_usage() const;
  // 清高亮/公式/Mermaid 内存缓存；不动持久缓存。换文档时用
  void ClearMemoryCaches();
  // 清计数；loaded/permanently_failed 等引擎状态不动
  void ResetStats();

 private:
  friend struct MarkdownRenderContextAccess;
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

// 便捷重载（两参 HighlightCode / FormulaToImgTag / MermaidToImgTag 与旧
// Clear*Cache）共用的进程默认上下文。持久缓存默认关闭：直接调用者不应
// 悄悄往用户缓存目录写文件，需要持久复用的宿主用 MarkdownView。
//
// **只能在 GUI 线程调用**：函数级静态只保证初始化安全，不保证后续访问；
// 渲染器本身还依赖 GUI 线程的字体度量与 QSvgRenderer
MarkdownRenderContext& DefaultRenderContext();

#endif  // FILE_MARKDOWN_MARKDOWN_RENDER_CONTEXT_H_
