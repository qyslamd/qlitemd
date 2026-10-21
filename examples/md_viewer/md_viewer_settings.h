#ifndef FILE_EXAMPLES_MD_VIEWER_MD_VIEWER_SETTINGS_H_
#define FILE_EXAMPLES_MD_VIEWER_MD_VIEWER_SETTINGS_H_

#include <QByteArray>
#include <QString>
#include <QStringList>

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

  // 最近的放最前，最多 10 条
  void AddRecentFile(const QString& path);

  QByteArray window_geometry;
  QByteArray dock_state;
  bool dark_theme = false;
  bool window_blur = true;
  bool code_line_numbers = false;
  bool formula_rendering = true;
  bool mermaid_rendering = true;
  bool persistent_cache = true;
  QStringList recent_files;
};

#endif  // FILE_EXAMPLES_MD_VIEWER_MD_VIEWER_SETTINGS_H_
