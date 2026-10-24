#ifndef FILE_MARKDOWN_HEADING_IDS_H_
#define FILE_MARKDOWN_HEADING_IDS_H_

#include <functional>

#include <QString>

#include "markdown_types.h"

// 标题锚点回调：每个 <h1>–<h6> 补完 id 后调一次（slug，元素 id）。
// 与 CodeBlockSink 同一套理由：宿主拿它把 <a href="#slug"> 反查成要滚的
// 元素，而 markdown 模块本身不认识"滚动"这件事
using HeadingSink = std::function<void(const QString& slug, const QString& id)>;

// 大纲回调：text 是去掉内联标签后的纯文本，anchor 是最终锚点（重名标题
// 带 -1/-2 后缀），可直接用于 ScrollToAnchor()。与 MarkdownRenderOptions
// 里的回调同一个类型，不需要中间适配层

// 给 md4c 输出的 <h1>–<h6> 补 GitHub 风味的 slug 与元素 id，双份都挂在
// 标签上：data-md-slug 放 slug（可能与用户写的锚点一致），id 放纯 ASCII 的
// h-N——litehtml 解析时会改写 id 的存储形式，非 ASCII 存不住，
// scrollToAnchor 会静默找不到元素（见 markdown_to_html.cpp 的说明）。
//
// GitHub 风味 = 保留字母/数字/CJK、空白转 '-'、保留 - 与 _、其余标点丢弃；
// 重名标题追加 -1 / -2 后缀（与 GitHub 一致）。输入是还没补 id 的正文 HTML。
QString AddHeadingIds(
    const QString& html, const HeadingSink& on_heading = {},
    const std::function<void(const MarkdownHeading&)>& on_outline = {});

#endif  // FILE_MARKDOWN_HEADING_IDS_H_
