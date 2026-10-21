#include "markdown_theme.h"

#include <QtCore/qglobal.h>  // Q_INIT_RESOURCE

// qrc 初始化。Q_INIT_RESOURCE 展开后是一个 extern 声明加一次调用，必须留在
// 全局命名空间：放进匿名命名空间会把声明绑到内部链接，链接期找不到 rcc
// 生成的函数。qrc 名只在这里写一次。
void EnsureThemeResources() { Q_INIT_RESOURCE(markdown_res); }

namespace {

const char* const kThemeCss[kMarkdownThemeCount] = {
    ":/markdown/themes/light.css",
    ":/markdown/themes/dark.css",
};

}  // namespace

// 加主题时这里少了 case 会静默落到 Light；枚举规模与 kMarkdownThemeCount
// 对不上则编译失败，逼着同步整张表
static_assert(static_cast<int>(MarkdownTheme::Dark) + 1 == kMarkdownThemeCount,
              "新增主题后同步 kMarkdownThemeCount / ThemeIndex / 主题表");

int ThemeIndex(MarkdownTheme theme) {
  switch (theme) {
    case MarkdownTheme::Light:
      return 0;
    case MarkdownTheme::Dark:
      return 1;
  }
  return 0;
}

QString ThemeCssResource(MarkdownTheme theme) {
  // 所有主题样式都从 qrc 读；首次取路径前确保资源已注册。幂等
  EnsureThemeResources();
  return QString::fromLatin1(kThemeCss[ThemeIndex(theme)]);
}

QColor ThemeFormulaColor(MarkdownTheme theme) {
  switch (theme) {
    case MarkdownTheme::Light:
      return QColor(0x24, 0x29, 0x2e);
    case MarkdownTheme::Dark:
      return QColor(0xe6, 0xed, 0xf3);
  }
  return QColor(0x24, 0x29, 0x2e);
}
