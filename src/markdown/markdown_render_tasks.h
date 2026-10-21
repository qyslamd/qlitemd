#ifndef FILE_MARKDOWN_MARKDOWN_RENDER_TASKS_H_
#define FILE_MARKDOWN_MARKDOWN_RENDER_TASKS_H_

#include <atomic>
#include <memory>

#include <QColor>
#include <QString>
#include <QVector>

#include "markdown_to_html.h"
#include "markdown_types.h"

// 一个去重后的渲染任务。cache_key 与各渲染器的内存缓存 key 一致；
// indices 是原文里同 key 的公式段/代码块序号，用于回填和进度统计
struct MarkdownRenderTask {
  MarkdownRenderKind kind = MarkdownRenderKind::Highlight;
  QString view_id;
  quint64 request_id = 0;
  QString cache_key;
  QString payload;
  QString language;
  bool display = false;
  QVector<int> indices;
  MarkdownTheme theme = MarkdownTheme::Light;
  QColor formula_color;
  MarkdownDiagnostics diagnostics;
  int worker_id = -1;
  // worker 渲染前装到 JsRuntime；取消后必须重建 runtime
  std::shared_ptr<std::atomic<bool>> cancel_flag;
};

// 从解析结果收集任务：公式按 (tex, display, color) 去重；每个代码块都
// 产生高亮任务；mermaid 围栏额外产生 mermaid 任务，高亮任务保留作失败回退
QVector<MarkdownRenderTask> CollectRenderTasks(
    const MarkdownParseResult& parsed, bool mermaid_enabled,
    const QColor& formula_color, MarkdownTheme theme);

#endif  // FILE_MARKDOWN_MARKDOWN_RENDER_TASKS_H_
