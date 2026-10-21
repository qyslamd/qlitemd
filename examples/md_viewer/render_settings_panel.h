#ifndef FILE_EXAMPLES_MD_VIEWER_RENDER_SETTINGS_PANEL_H_
#define FILE_EXAMPLES_MD_VIEWER_RENDER_SETTINGS_PANEL_H_

#include <QWidget>

#include "markdown_types.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QVBoxLayout;
class MarkdownView;

// 渲染设置面板：主题、行号、公式/Mermaid 开关、持久缓存、诊断。
// 渲染状态在独立的 RenderStatusPanel（另一个 dock）。直接读写 MarkdownView
// 的状态；控件只用 Fluent 的属性驱动（isCard / isSwitchButton），不写样式表
class RenderSettingsPanel : public QWidget {
  Q_OBJECT

 public:
  explicit RenderSettingsPanel(MarkdownView* view, QWidget* parent = nullptr);

  // 外部（标题栏主题按钮）改了文档主题时同步下拉框，不再反向触发
  void SetTheme(MarkdownTheme theme);
  // 启动时按持久化设置同步模糊开关，不触发重新应用
  void SetWindowBlurEnabled(bool enabled);

 signals:
  void statusMessage(const QString& message);
  // 用户在面板里改了主题：宿主负责把文档与 Fluent 壳一起切
  void themeChanged(MarkdownTheme theme);
  // Windows：标题栏/窗口的 DWM 模糊开关
  void windowBlurEnabledChanged(bool enabled);

 private:
  void initUi();
  void initSignals();
  void ApplyDiagnostics();
  void BrowseCacheDirectory();
  void BrowseDumpDirectory();
  // 开关默认文字跟状态走（Fluent Gallery 同款写法）
  QCheckBox* CreateSwitch(bool checked);

  MarkdownView* view_ = nullptr;
  QComboBox* theme_ = nullptr;
  // 仅 Windows 创建
  QCheckBox* window_blur_ = nullptr;
  QCheckBox* code_line_numbers_ = nullptr;
  QCheckBox* formula_rendering_ = nullptr;
  QCheckBox* mermaid_rendering_ = nullptr;
  QCheckBox* persistent_cache_ = nullptr;
  QLineEdit* cache_directory_ = nullptr;
  QPushButton* choose_cache_ = nullptr;
  QPushButton* reset_cache_ = nullptr;
  QPushButton* clear_memory_cache_ = nullptr;
  QPushButton* clear_cache_ = nullptr;
  QCheckBox* trace_ = nullptr;
  QLineEdit* dump_directory_ = nullptr;
  QPushButton* choose_dump_ = nullptr;
  QPushButton* reset_dump_ = nullptr;
};

#endif  // FILE_EXAMPLES_MD_VIEWER_RENDER_SETTINGS_PANEL_H_
