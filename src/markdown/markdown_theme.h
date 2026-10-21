#ifndef FILE_MARKDOWN_MARKDOWN_THEME_H_
#define FILE_MARKDOWN_MARKDOWN_THEME_H_

#include <QColor>
#include <QString>

#include "markdown_types.h"

// 主题总数。新增枚举值时必须同步 ThemeIndex/ThemeCssResource/公式色；
// markdown_theme.cpp 里的 static_assert 会盯住枚举规模
constexpr int kMarkdownThemeCount = 2;

// 枚举 → 紧凑下标（0..kMarkdownThemeCount-1），供各主题表按行索引
int ThemeIndex(MarkdownTheme theme);

// 主题 → qrc 样式表路径（别名见 markdown_res.qrc）。MD_MARKDOWN_CSS 覆盖
// 时样式表不从这个路径读，但映射本身仍集中在这里
QString ThemeCssResource(MarkdownTheme theme);

// 公式 SVG 的前景色。MathJax 输出里的 currentColor 会被替换成它，因此
// 颜色参与公式缓存 key——两套主题必须给出不同的颜色
QColor ThemeFormulaColor(MarkdownTheme theme);

#endif  // FILE_MARKDOWN_MARKDOWN_THEME_H_
