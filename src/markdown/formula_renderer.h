#ifndef FILE_MARKDOWN_FORMULA_RENDERER_H_
#define FILE_MARKDOWN_FORMULA_RENDERER_H_

#include <QColor>
#include <QString>

class MarkdownRenderContext;

// TeX 公式 → 可直接内联进 HTML 的 <img>（src 是 PNG 的 data URI）。
//
// 为什么是图：qlitehtml 排不动 KaTeX/MathJax 的 vlist 布局（inline-table 不
// 收缩到内容宽、负偏移塌陷——实测分式分数线拉成通栏），但画图没问题。所以
// 排版交给 QuickJS 里的 MathJax（tex→svg），Qt 再把 SVG 画成位图。
//
// 首次调用才加载 JS 载荷（1.5MB 且首次求值要一两秒），没公式的文档不付这个
// 钱；结果按 (tex, display) 缓存。渲染失败返回空串——调用方按"显示原文"兜底。
//
// qrc 载荷的初始化由本模块内部完成（Q_INIT_RESOURCE
// 幂等），宿主不需要做任何事。
//
// ctx 版本：缓存与计数记在该上下文；color 有效时替换 SVG 里的
// currentColor，并参与缓存 key。两参 ctx 重载保持旧输出（不换色）
QString FormulaToImgTag(MarkdownRenderContext& context, const QString& tex,
                        bool display);
// cache_key 非空时写入本次结果的缓存 key（内容稳定，供宿主生成图片 token）；
// 内存命中、磁盘命中、渲染失败也会写。调用方不要拿它做展示。
QString FormulaToImgTag(MarkdownRenderContext& context, const QString& tex,
                        bool display, const QColor& color,
                        QString* cache_key = nullptr);
// 便捷重载：进程默认上下文，旧行为
QString FormulaToImgTag(const QString& tex, bool display);

// 内存/任务去重用的稳定 key；与 FormulaToImgTag 的缓存规则一致
QString FormulaRenderKey(const QString& tex, bool display, const QColor& color);

// JS 载荷是否可用（加载失败即永久不可用，不会反复重试）
bool FormulaRendererReady();

// 清空**进程默认上下文**的公式缓存（tag 是 KB 级，长会话里换文档后旧公式
// 没有复用价值）。运行时不释放、载荷不重载——只丢结果。
// MarkdownView 用自己的上下文：宿主请调 MarkdownView::ClearRenderCaches()
// 或 MarkdownRenderContext::ClearMemoryCaches()
void ClearFormulaCache();

#endif  // FILE_MARKDOWN_FORMULA_RENDERER_H_
