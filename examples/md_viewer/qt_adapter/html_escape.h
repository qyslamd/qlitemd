#ifndef FILE_MARKDOWN_HTML_ESCAPE_H_
#define FILE_MARKDOWN_HTML_ESCAPE_H_

#include <QString>

// HTML 文本与双引号属性值的转义。字符集与 md4c-html 的
// render_html_escaped 一致（& < > "）——md4c 先把源码实体解码成 UTF-8、
// 再按这四字符重转义，所以两边产物可以互相接续
QString EscapeHtml(const QString& text);

// EscapeHtml 的逆操作。解码集是 md4c 产物（&amp; &lt; &gt; &quot;）加上
// 原始 HTML 直通时可能出现的撇号实体（&#39; / &apos;）
QString UnescapeHtml(const QString& text);

#endif  // FILE_MARKDOWN_HTML_ESCAPE_H_
