#include "markdown_render_tasks.h"

#include <QHash>

#include "code_highlighter.h"
#include "formula_renderer.h"
#include "mermaid_renderer.h"

namespace {

QString TaskIdentity(MarkdownRenderKind kind, const QString& cache_key) {
  return QString::number(static_cast<int>(kind)) + QLatin1Char('\n') +
         cache_key;
}

}  // namespace

QVector<MarkdownRenderTask> CollectRenderTasks(
    const MarkdownParseResult& parsed, bool mermaid_enabled,
    const QColor& formula_color, MarkdownTheme theme) {
  QVector<MarkdownRenderTask> tasks;
  if (parsed.parse_error) {
    return tasks;
  }
  QHash<QString, int> task_by_identity;
  auto add_task = [&tasks, &task_by_identity, theme, formula_color](
                      MarkdownRenderKind kind, const QString& cache_key,
                      const QString& payload, const QString& language,
                      bool display, int index) {
    const QString identity = TaskIdentity(kind, cache_key);
    const auto it = task_by_identity.constFind(identity);
    if (it != task_by_identity.constEnd()) {
      tasks[it.value()].indices.append(index);
      return;
    }
    MarkdownRenderTask task;
    task.kind = kind;
    task.cache_key = cache_key;
    task.payload = payload;
    task.language = language;
    task.display = display;
    task.theme = theme;
    task.formula_color = formula_color;
    task.indices.append(index);
    task_by_identity.insert(identity, tasks.size());
    tasks.append(task);
  };

  for (int i = 0; i < parsed.segments.size(); ++i) {
    const MarkdownMathSegment& segment = parsed.segments.at(i);
    add_task(MarkdownRenderKind::Formula,
             FormulaRenderKey(segment.tex, segment.display, formula_color),
             segment.tex, QString(), segment.display, i);
  }

  for (const MarkdownCodeBlock& block : parsed.blocks) {
    add_task(MarkdownRenderKind::Highlight,
             HighlightRenderKey(block.code, block.language), block.code,
             block.language, false, block.index);
    if (mermaid_enabled && block.language == QLatin1String("mermaid")) {
      add_task(MarkdownRenderKind::Mermaid, MermaidRenderKey(block.code, theme),
               block.code, QString(), false, block.index);
    }
  }
  return tasks;
}
