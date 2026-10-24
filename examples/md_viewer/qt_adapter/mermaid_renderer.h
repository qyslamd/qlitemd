#ifndef FILE_MARKDOWN_MERMAID_RENDERER_H_
#define FILE_MARKDOWN_MERMAID_RENDERER_H_

#include <QString>

enum class MarkdownTheme;

// mermaid 渲染的 key/variant 与共享配置。渲染本体是 markdown_engine_state.h
// 的 RenderMermaidOnEngine（worker 直接调用，quickjs / merman 双后端）；
// key 同时是图片 token 的哈希输入，二者必须同源。

// 内存/任务去重用的稳定 key；主题与字体指纹参与
QString MermaidRenderKey(const QString& code, MarkdownTheme theme);
// key 的 variant 部分（主题|字体指纹[|merman 版本]），磁盘缓存 key 也要用
QString MermaidRenderVariant(MarkdownTheme theme);

// 度量与绘制共用的字体族名。空串表示没找到带 CJK 的族，此时不改写 SVG 里的
// font-family（度量按 SVG 自己写的族名走）。供排查与单测用。
//
// **前置条件：GUI 线程、QGuiApplication 已构造之后**——内部要查 QFontDatabase，
// 度量也要建 QFontMetricsF，两者都依赖 GUI 应用实例。将来若把渲染搬进后台线程，
// 线程里得先有 QGuiApplication（或把族名在 GUI 线程算好传进去）。
QString MermaidFontFamily();

#endif  // FILE_MARKDOWN_MERMAID_RENDERER_H_
