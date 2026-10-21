#ifndef FILE_MARKDOWN_MERMAID_RENDERER_H_
#define FILE_MARKDOWN_MERMAID_RENDERER_H_

#include <QString>

class MarkdownRenderContext;
enum class MarkdownTheme;

// mermaid 源码 → 可直接内联进 HTML 的 <img>（src 是 PNG 的 data URI）。
//
// 为什么不引 Node/WebEngine：与公式那条路同一套约束——QuickJS 里跑官方
// mermaid.js 出 SVG，Qt 再把 SVG 画成位图。mermaid 与 MathJax 的差别在于它
// 要问宿主"这段字多宽"才决定连线怎么走，所以多一份假 DOM（dom_shim.js）和
// 一组度量回调（用 QFontMetricsF 回答，与绘制共用同一份字体）。
//
// 三份载荷放在 qrc 里（3.5MB），首次调用才求值；结果按源码缓存，**渲染失败
// 也缓存空串**（同一个坏语法不该每次编辑都重试）。载荷加载或主题 initialize
// 失败走一次性标志（FailLoad，作废引擎），不会每次渲染都重试。渲染失败返回空串。
//
// qrc 载荷的初始化由本模块内部完成（Q_INIT_RESOURCE
// 幂等），宿主不需要做任何事。
//
// ctx 版本：缓存与计数记在该上下文；theme 参与缓存 key，并通过每张图的
// %%{init}%% 指令生效（initialize 只做一次，多视图主题互不污染）。便捷重载
// 固定按 Light 渲染，不依赖引擎里残留的主题状态
// cache_key 非空时写入本次结果的缓存 key（内容稳定，供宿主生成图片 token）；
// 内存命中、磁盘命中、渲染失败也会写。调用方不要拿它做展示。
QString MermaidToImgTag(MarkdownRenderContext& context, const QString& code,
                        MarkdownTheme theme, QString* cache_key = nullptr);
QString MermaidToImgTag(const QString& code);

// 内存/任务去重用的稳定 key；主题与字体指纹参与
QString MermaidRenderKey(const QString& code, MarkdownTheme theme);
// key 的 variant 部分（主题|字体指纹），磁盘缓存 key 也要用
QString MermaidRenderVariant(MarkdownTheme theme);

// 载荷是否可用（加载失败即永久不可用，不会反复重试）
bool MermaidRendererReady();

// 清空**进程默认上下文**的图缓存（每张是内嵌 PNG 的 data URI，大图 MB 级；
// 换文档后旧图没有复用价值，长会话里那几十 MB 该还回去）。运行时不释放、
// 载荷不重载。MarkdownView 用自己的上下文：宿主请调
// MarkdownView::ClearRenderCaches() 或
// MarkdownRenderContext::ClearMemoryCaches()
void ClearMermaidCache();

// 度量与绘制共用的字体族名。空串表示没找到带 CJK 的族，此时不改写 SVG 里的
// font-family（度量按 SVG 自己写的族名走）。供排查与单测用。
//
// **前置条件：GUI 线程、QGuiApplication 已构造之后**——内部要查 QFontDatabase，
// 度量也要建 QFontMetricsF，两者都依赖 GUI 应用实例。将来若把渲染搬进后台线程，
// 线程里得先有 QGuiApplication（或把族名在 GUI 线程算好传进去）。
QString MermaidFontFamily();

#endif  // FILE_MARKDOWN_MERMAID_RENDERER_H_
