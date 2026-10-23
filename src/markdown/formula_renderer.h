#ifndef FILE_MARKDOWN_FORMULA_RENDERER_H_
#define FILE_MARKDOWN_FORMULA_RENDERER_H_

#include <QColor>
#include <QString>

// TeX 公式渲染的缓存/任务 key。渲染本体是 markdown_engine_state.h 的
// RenderFormulaOnEngine（worker 直接调用），key 同时是图片 token 的哈希
// 输入，二者必须同源。
//
// MathJax 在数学模式下对首尾空白不敏感：key 与渲染输入统一用 trim 后的
// 文本。color 有效时参与 key（暗色主题的公式配色独立缓存）
QString FormulaRenderKey(const QString& tex, bool display, const QColor& color);

#endif  // FILE_MARKDOWN_FORMULA_RENDERER_H_
