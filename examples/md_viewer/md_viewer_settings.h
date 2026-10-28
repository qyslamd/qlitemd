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

// md_viewer 的最小设置：标准配置目录下的 md_viewer.ini。
// 只存这个示例自己需要的项，不引入应用级配置体系。
struct MdViewerSettings {
  // QStandardPaths::AppConfigLocation + /md_viewer.ini
  static QString DefaultPath();

  void Load();
  // 全量落盘。最近文件单独走 SaveRecent，避免打开文件时用旧快照
  // 覆盖掉当前窗口状态
  void Save() const;
  void SaveRecent() const;
  // 只写根目录与上次文档，根变化/切篇时立即落盘
  void SaveSession() const;
  void SaveBookmarks() const;
  void SavePositions() const;

  // 最近的放最前，最多 10 条
  void AddRecentFile(const QString& path);

  QByteArray window_geometry;
  QByteArray dock_state;
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
  bool formula_rendering = true;
  bool mermaid_rendering = true;
  bool persistent_cache = true;
  QStringList recent_files;
  QString root_dir;
  QString last_file;
  QString bookmarks_json;
  QString positions_json;
};

#endif  // FILE_EXAMPLES_MD_VIEWER_MD_VIEWER_SETTINGS_H_
