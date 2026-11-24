#ifndef FILE_APPS_QT_MD_VIEWER_READER_APPEARANCE_POPUP_H_
#define FILE_APPS_QT_MD_VIEWER_READER_APPEARANCE_POPUP_H_

#include <QWidget>

#include "reader_preferences.h"

class QButtonGroup;
class QKeyEvent;

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

#endif  // FILE_APPS_QT_MD_VIEWER_READER_APPEARANCE_POPUP_H_
