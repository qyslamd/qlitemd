#ifndef FILE_EXAMPLES_MD_VIEWER_MD_VIEWER_SETTINGS_H_
#define FILE_EXAMPLES_MD_VIEWER_MD_VIEWER_SETTINGS_H_

#include <QByteArray>
#include <QString>
#include <QStringList>

#include "markdown_types.h"

enum class WindowMaterial {
  kAuto = 0,
  kAcrylic = 1,
  kNone = 2,
};

enum class ReadingStyle {
  kLibraryDefault = 0,
  kPrototype = 1,
};

// qt_md_viewer 的最小设置：标准配置目录下的 qt_md_viewer.ini。
// 只存这个应用自己需要的项，不引入应用级配置体系。
struct MdViewerSettings {
  // QStandardPaths::AppConfigLocation + /qt_md_viewer.ini
  static QString DefaultPath();

  void Load();
  // 首次启动把旧版 md_viewer.ini 复制成 qt_md_viewer.ini
  static void MigrateLegacyFile();
  // 全量落盘。最近文件单独走 SaveRecent，避免打开文件时用旧快照
  // 覆盖掉当前窗口状态
  void Save() const;
  void SaveRecent() const;
  // 只写根目录与上次文档，根变化/切篇时立即落盘
  void SaveSession() const;
  void SaveBookmarks() const;
  void SavePositions() const;
  void SaveRecentRoots() const;

  // 最近的放最前，最多 10 条
  void AddRecentFile(const QString& path);
  // 最近打开的工作区，最多 8 条
  void AddRecentRoot(const QString& root);

  QByteArray window_geometry;
  // 左侧活动栏当前视图（0 文档 / 1 搜索 / 2 收藏 / 3 渲染状态）
  int sidebar_view = 0;
  bool sidebar_visible = true;
  int sidebar_width = 280;
  int outline_width = 212;
  // 大纲列是否由用户打开；窄窗自动隐藏不写回这里
  bool outline_visible = true;
  bool dark_theme = false;
  WindowMaterial window_material = WindowMaterial::kAuto;
  // opt-in：默认库样式；原型样式由用户主动启用
  ReadingStyle reading_style = ReadingStyle::kLibraryDefault;
  // 仅原型样式生效；关闭时用主题页面底色
  bool transparent_reading = true;
  // 阅读外观：0 / -1 表示跟随样式，不注入覆盖 CSS
  int reading_font_size = 0;
  int reading_line_height_percent = 0;
  int reading_measure_px = -1;
  bool code_line_numbers = false;
  MarkdownAdmonitionMode admonition_mode = MarkdownAdmonitionMode::kMpe;
  MarkdownMermaidBackend mermaid_backend = MarkdownMermaidBackend::kAuto;
  MarkdownFormulaBackend formula_backend = MarkdownFormulaBackend::kAuto;
  bool formula_rendering = true;
  bool mermaid_rendering = true;
  bool persistent_cache = true;
  QStringList recent_files;
  QStringList recent_roots;
  QString root_dir;
  QString last_file;
  // TabModel 序列化的 open_tabs + active；多标签会话恢复用
  QByteArray open_tabs_json;
  // 用户显式卸载了工作区：重启回欢迎页，不自动恢复最近文档。
  // 打开任意文件/目录即清除
  bool workspace_closed = false;
  QString bookmarks_json;
  QString positions_json;
  // 次窗口用的是主窗口设置的只读副本：所有写入口直接返回
  bool read_only = false;
};

#endif  // FILE_EXAMPLES_MD_VIEWER_MD_VIEWER_SETTINGS_H_
