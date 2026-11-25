#ifndef FILE_APPS_QT_MD_VIEWER_SETTINGS_PANEL_H_
#define FILE_APPS_QT_MD_VIEWER_SETTINGS_PANEL_H_

#include <QDialog>
#include <QPointer>
#include <QWidget>

#include "markdown_types.h"
#include "md_viewer_settings.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QStackedWidget;
class QVBoxLayout;
class MarkdownView;

// 设置对话框的内容：外观/阅读/内容/缓存/诊断五个分类页。
// 渲染状态在侧栏的 RenderStatusPanel 独立页。直接读写 MarkdownView 的
// 状态；控件只用 Fluent 的属性驱动（isCard / isSwitchButton），不写样式表
class SettingsPanel : public QWidget {
  Q_OBJECT

 public:
  explicit SettingsPanel(MarkdownView* view, QWidget* parent = nullptr);

  // 切标签时改绑到新的活动视图；全局设置各标签一致，控件无需重读
  void SetView(MarkdownView* view);
  // 外部（标题栏主题按钮）改了文档主题时同步下拉框，不再反向触发
  void SetTheme(MarkdownTheme theme);
  // 启动时按持久化设置同步材质下拉框，不触发重新应用
  void SetWindowMaterial(WindowMaterial material);
  // 启动时按持久化设置同步阅读区选项，不触发重新应用
  void SetReadingStyle(ReadingStyle style);
  void SetTransparentReading(bool enabled);

 signals:
  void statusMessage(const QString& message);
  // 用户在面板里改了主题：宿主负责把文档与 Fluent 壳一起切
  void themeChanged(MarkdownTheme theme);
  // Windows：窗口背景材质（Mica/亚克力/关闭）
  void windowMaterialChanged(WindowMaterial material);
  // 阅读区样式 opt-in 与透明页面
  void readingStyleChanged(ReadingStyle style);
  void transparentReadingChanged(bool enabled);
  // 面板直接写进活动视图的设置变了；宿主据此广播到其它标签
  void settingsChanged();

 private:
  void initUi();
  void initSignals();
  void ApplyDiagnostics();
  void BrowseCacheDirectory();
  void BrowseDumpDirectory();
  void UpdateReadingControls();
  // 开关默认文字跟状态走（Fluent Gallery 同款写法）
  QCheckBox* CreateSwitch(bool checked);

  // 视图可能随标签关闭而销毁；QPointer 自动置空，槽里再判空
  QPointer<MarkdownView> view_;
  QListWidget* category_list_ = nullptr;
  QStackedWidget* page_stack_ = nullptr;
  QComboBox* theme_ = nullptr;
  // 仅 Windows 创建
  QComboBox* window_material_ = nullptr;
  QComboBox* reading_style_ = nullptr;
  QCheckBox* transparent_reading_ = nullptr;
  QComboBox* admonition_mode_ = nullptr;
  QComboBox* mermaid_backend_ = nullptr;
  QComboBox* formula_backend_ = nullptr;
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

// 设置对话框外壳：托管 SettingsPanel。模型窗口，改动即时生效，没有
// 应用/取消按钮；关闭只是隐藏，下次打开保留滚动位置。
class SettingsDialog : public QDialog {
  Q_OBJECT

 public:
  explicit SettingsDialog(QWidget* content, QWidget* parent = nullptr);
};

#endif  // FILE_APPS_QT_MD_VIEWER_SETTINGS_PANEL_H_
