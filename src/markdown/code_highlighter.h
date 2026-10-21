#ifndef FILE_MARKDOWN_CODE_HIGHLIGHTER_H_
#define FILE_MARKDOWN_CODE_HIGHLIGHTER_H_

#include <QString>

class MarkdownRenderContext;

// 源码文本 → 带高亮 <span> 的 HTML 片段。实现在 QuickJS 里跑 highlight.js
// （载荷在 qrc，66KB，配方见 resources/highlight_entry.js 头注释），qrc 初始化
// 由本模块内部完成，宿主不需要做任何事。
//
// 类名是与主题样式表（resources/themes/*.css）的唯一契约：kw/str/num/cmt
// 之外还有 ty/bi/fn/tt/lit/var/re/meta/attr/tag/sel/sym/op——映射表在 entry 的
// CLASS_MAP 里，改配色只动 CSS。language 未注册时返回纯转义的原文（不猜
// 语言）；输出已做 HTML 转义，可直接进 HTML。
//
// ctx 版本把计数记到该上下文；便捷重载走进程默认上下文
QString HighlightCode(MarkdownRenderContext& context, const QString& code,
                      const QString& language);
QString HighlightCode(const QString& code, const QString& language);

// 内存/任务去重用的稳定 key；调度与渲染层共用
QString HighlightRenderKey(const QString& code, const QString& language);

#endif  // FILE_MARKDOWN_CODE_HIGHLIGHTER_H_
