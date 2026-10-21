#ifndef FILE_MARKDOWN_MERMAID_SVG_CLEANUP_H_
#define FILE_MARKDOWN_MERMAID_SVG_CLEANUP_H_

#include <QString>

// 删掉 mermaid 产出里 QSvgRenderer 画不了的东西，并把文字位置定标回 <text>。
//
// 三条修补：删无尺寸的 <rect class="background">（会被画成黑块）、
// stroke-dasharray="0" 改成 none（不然整条线不画）、把定位 tspan 上的
// x/y/dy 烘焙到 <text>（Qt 的 QSvgTspan 只存文字与样式，不存位置，不烘焙
// 文字会整体偏高）。
//
// 必须在 CSS 内联之前调用：text-anchor 之类的规则要作用到新造出来的
// <text> 元素上。
QString CleanSvg(const QString& svg);

// 把 SVG 里 <style> 块的 CSS 规则内联到各元素的属性上。
//
// QSvgRenderer 不认 <style> 块（mermaid 的样式 99% 在里面），而且它处理 style
// 与 presentation attribute 的优先级跟 CSS 规范相反——它认 attribute，所以命中
// 的声明要同时写进属性。
//
// font_family 非空时，所有元素的 font-family 都被强制成它：mermaid 写的是
// "trebuchet ms",verdana,...，QSvgRenderer 会去系统里找替代字体，与度量用的
// 那份对不上（量画不同源）。传空串表示不改写。
QString InlineSvgCss(const QString& svg, const QString& font_family);

// 把 marker-start / marker-end 在端点处展开成普通 <g>，最后删掉 <marker> 定义。
//
// QSvgRenderer 不画 marker，还会把定义里的 path 误画到原点，而删掉定义又会
// 让引用悬空、整条 path 不画。必须在 CSS 内联之后调用：箭头颜色来自类规则
// （sequence 的 <line> 属性上写的就是 stroke="none"），早一步读到的是 none，
// 箭头会画成隐形的。
QString InlineSvgMarkers(const QString& svg);

#endif  // FILE_MARKDOWN_MERMAID_SVG_CLEANUP_H_
