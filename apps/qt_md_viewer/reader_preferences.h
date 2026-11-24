#ifndef FILE_APPS_QT_MD_VIEWER_READER_PREFERENCES_H_
#define FILE_APPS_QT_MD_VIEWER_READER_PREFERENCES_H_

#include <QMetaType>
#include <QString>

// 阅读偏好。0 / -1 表示“跟随样式”，因此默认值不会注入任何 CSS。
struct ReaderPreferences {
  int font_size = 0;            // px；0 = 主题默认
  int line_height_percent = 0;  // 178 = 1.78；0 = 主题默认
  int measure_px = -1;          // -1 = 默认；0 = 全宽；其余 = 像素宽
  bool IsDefault() const {
    return font_size == 0 && line_height_percent == 0 && measure_px == -1;
  }
};

Q_DECLARE_METATYPE(ReaderPreferences)

// 生成追加到主题 CSS 末尾的覆盖块。prototype_layout=true 时栏宽落在
// 标题/正文/列表/引用上（媒体保持全宽）；否则落在 .md-page 上。
QString ReaderAppearanceCss(const ReaderPreferences& preferences,
                            bool prototype_layout);

#endif  // FILE_APPS_QT_MD_VIEWER_READER_PREFERENCES_H_
