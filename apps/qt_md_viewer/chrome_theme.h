#ifndef FILE_APPS_QT_MD_VIEWER_CHROME_THEME_H_
#define FILE_APPS_QT_MD_VIEWER_CHROME_THEME_H_

#include <QColor>

// 窗口 chrome 的配色：标题栏、标签条、工具栏共用。
// 颜色保留 alpha，Blur/Acrylic 才能从 chrome 上透出来。
struct ChromeColors {
  QColor background;
  QColor selected;
  QColor hover;
  QColor separator;
  QColor selected_text;
  QColor normal_text;
};

inline ChromeColors ChromeColorsFor(bool dark) {
  ChromeColors colors;
  if (dark) {
    colors.background = QColor(0x2e, 0x2f, 0x30, 160);
    colors.selected = QColor(0x4a, 0x4a, 0x4a, 200);
    colors.hover = QColor(0x5a, 0x5a, 0x5a, 80);
    colors.separator = QColor(0xd3, 0xd3, 0xd5);
    colors.selected_text = QColor(Qt::white);
    colors.normal_text = QColor(0xd3, 0xd3, 0xd5);
  } else {
    colors.background = QColor(0xce, 0xce, 0xce, 160);
    colors.selected = QColor(0xf9, 0xf9, 0xfb, 200);
    colors.hover = QColor(255, 255, 255, 76);
    colors.separator = QColor(0x4b, 0x4b, 0x4b);
    colors.selected_text = QColor(Qt::black);
    colors.normal_text = QColor(0x66, 0x66, 0x66);
  }
  return colors;
}

#endif  // FILE_APPS_QT_MD_VIEWER_CHROME_THEME_H_
