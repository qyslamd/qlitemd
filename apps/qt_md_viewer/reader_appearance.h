#ifndef FILE_EXAMPLES_MD_VIEWER_READER_APPEARANCE_H_
#define FILE_EXAMPLES_MD_VIEWER_READER_APPEARANCE_H_

#include <QMetaType>
#include <QString>
#include <QWidget>

class QButtonGroup;
class QKeyEvent;

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

// 标题栏 “Aa” 按钮的弹出面板：字号、行距、栏宽三组选项 + 恢复默认。
class ReaderAppearancePopup : public QWidget {
  Q_OBJECT

 public:
  explicit ReaderAppearancePopup(QWidget* parent = nullptr);

  void SetPreferences(const ReaderPreferences& preferences);
  ReaderPreferences preferences() const { return preferences_; }
  // 贴在锚点控件下方，自动避开屏幕边缘
  void OpenNear(QWidget* anchor);

 signals:
  void preferencesChanged(const ReaderPreferences& preferences);

 protected:
  void keyPressEvent(QKeyEvent* event) override;

 private:
  void UpdateChecked();

  ReaderPreferences preferences_;
  QButtonGroup* font_group_ = nullptr;
  QButtonGroup* line_group_ = nullptr;
  QButtonGroup* measure_group_ = nullptr;
};

#endif  // FILE_EXAMPLES_MD_VIEWER_READER_APPEARANCE_H_
