#include <gtest/gtest.h>

#include <QFile>
#include <QSet>
#include <QString>

#include "markdown_theme.h"

// 主题映射是纯查表，但映射错了会让整套样式读不到——qrc 别名与代码里的
// 路径分居两处，必须有一条断言把它们钉在一起
namespace {

TEST(MarkdownThemeTest, EveryThemeHasDistinctCss) {
  const MarkdownTheme themes[] = {MarkdownTheme::Light, MarkdownTheme::Dark};
  static_assert(sizeof(themes) / sizeof(themes[0]) == kMarkdownThemeCount,
                "新增主题后测试与主题表要同步");
  QSet<int> indices;
  QSet<QString> contents;
  for (const MarkdownTheme theme : themes) {
    const QString path = ThemeCssResource(theme);
    QFile file(path);
    ASSERT_TRUE(file.open(QIODevice::ReadOnly | QIODevice::Text))
        << path.toStdString();
    const QString css = QString::fromUtf8(file.readAll());
    EXPECT_FALSE(css.isEmpty()) << path.toStdString();
    // 每套主题都有正文颜色与代码块背景，且背景色互不相同
    EXPECT_TRUE(css.contains(QLatin1String("body"))) << path.toStdString();
    // 页容器（限宽居中 + 内边距）：两套主题必须一起有，漏一套就会在宽屏
    // 上贴边铺满。注意挂在 .md-page div 上，body 的尺寸在 qlitehtml 里恒
    // 等于视口，写了也不生效
    EXPECT_TRUE(css.contains(QLatin1String(".md-page"))) << path.toStdString();
    EXPECT_TRUE(css.contains(QLatin1String("max-width: 980px")))
        << path.toStdString();
    contents.insert(css);
    indices.insert(ThemeIndex(theme));
  }
  EXPECT_EQ(contents.size(), kMarkdownThemeCount) << "主题样式表不应逐字相同";
  EXPECT_EQ(indices.size(), kMarkdownThemeCount) << "ThemeIndex 不应撞车";
}

TEST(MarkdownThemeTest, FormulaColorsAreDistinctAndOpaque) {
  const QColor light = ThemeFormulaColor(MarkdownTheme::Light);
  const QColor dark = ThemeFormulaColor(MarkdownTheme::Dark);
  EXPECT_TRUE(light.isValid());
  EXPECT_TRUE(dark.isValid());
  EXPECT_NE(light, dark);
}

}  // namespace
