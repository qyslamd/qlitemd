#include "md_viewer_settings.h"

#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>

namespace {

constexpr int kMaxRecentFiles = 10;

constexpr char kKeyGeometry[] = "window/geometry";
constexpr char kKeyDockState[] = "window/dock_state";
constexpr char kKeyDarkTheme[] = "appearance/dark_theme";
constexpr char kKeyWindowMaterial[] = "appearance/window_material";
constexpr char kKeyReadingStyle[] = "appearance/reading_style";
constexpr char kKeyTransparentReading[] = "appearance/transparent_reading";
constexpr char kKeyReadingFontSize[] = "reading/font_size";
constexpr char kKeyReadingLineHeight[] = "reading/line_height_percent";
constexpr char kKeyReadingMeasure[] = "reading/measure_px";
// 旧键只用于迁移；新版本写它保持降级可读
constexpr char kKeyWindowBlur[] = "appearance/window_blur";
constexpr char kKeyCodeLineNumbers[] = "render/code_line_numbers";
constexpr char kKeyAdmonitionMode[] = "render/admonition_mode";
constexpr char kKeyMermaidBackend[] = "render/mermaid_backend";
constexpr char kKeyFormula[] = "render/formula";
constexpr char kKeyMermaid[] = "render/mermaid";
constexpr char kKeyPersistentCache[] = "render/persistent_cache";
constexpr char kKeyRecentFiles[] = "recent/files";
constexpr char kKeyRootDir[] = "root/dir";
constexpr char kKeyLastFile[] = "session/last_file";
constexpr char kKeyBookmarks[] = "bookmarks/files";
constexpr char kKeyPositions[] = "session/positions";

// Qt6 always stores INI files as UTF-8; setIniCodec is Qt5-only.
void SetIniUtf8(QSettings& settings) {
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
  settings.setIniCodec("UTF-8");
#else
  Q_UNUSED(settings);
#endif
}

// QSettings 负责建文件，不负责建父目录；写之前先把标准配置目录补出来
QString WritableSettingsPath() {
  const QString path = MdViewerSettings::DefaultPath();
  QDir().mkpath(QFileInfo(path).absolutePath());
  return path;
}

// 配置目录不可写时只提示一次，不要每次开文件都刷屏
void WarnWriteFailed(const QSettings& settings) {
  if (settings.status() == QSettings::NoError) {
    return;
  }
  static bool warned = false;
  if (!warned) {
    warned = true;
    qWarning() << "md_viewer: 无法写入设置文件" << settings.fileName();
  }
}

}  // namespace

QString MdViewerSettings::DefaultPath() {
  return QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) +
         QStringLiteral("/md_viewer.ini");
}

void MdViewerSettings::Load() {
  QSettings settings(DefaultPath(), QSettings::IniFormat);
  SetIniUtf8(settings);
  window_geometry = settings.value(kKeyGeometry).toByteArray();
  dock_state = settings.value(kKeyDockState).toByteArray();
  dark_theme = settings.value(kKeyDarkTheme, false).toBool();
  if (settings.contains(kKeyWindowMaterial)) {
    const int material = settings.value(kKeyWindowMaterial).toInt();
    window_material = material == static_cast<int>(WindowMaterial::kAcrylic)
                          ? WindowMaterial::kAcrylic
                      : material == static_cast<int>(WindowMaterial::kNone)
                          ? WindowMaterial::kNone
                          : WindowMaterial::kAuto;
  } else {
    window_material = settings.value(kKeyWindowBlur, true).toBool()
                          ? WindowMaterial::kAuto
                          : WindowMaterial::kNone;
  }
  reading_style = settings.value(kKeyReadingStyle, 0).toInt() == 1
                      ? ReadingStyle::kPrototype
                      : ReadingStyle::kLibraryDefault;
  transparent_reading = settings.value(kKeyTransparentReading, true).toBool();
  reading_font_size = settings.value(kKeyReadingFontSize, 0).toInt();
  reading_line_height_percent =
      settings.value(kKeyReadingLineHeight, 0).toInt();
  reading_measure_px = settings.value(kKeyReadingMeasure, -1).toInt();
  code_line_numbers = settings.value(kKeyCodeLineNumbers, false).toBool();
  admonition_mode = static_cast<MarkdownAdmonitionMode>(
      settings
          .value(kKeyAdmonitionMode,
                 static_cast<int>(MarkdownAdmonitionMode::kMpe))
          .toInt());
  mermaid_backend = static_cast<MarkdownMermaidBackend>(
      settings
          .value(kKeyMermaidBackend,
                 static_cast<int>(MarkdownMermaidBackend::kAuto))
          .toInt());
  formula_rendering = settings.value(kKeyFormula, true).toBool();
  mermaid_rendering = settings.value(kKeyMermaid, true).toBool();
  persistent_cache = settings.value(kKeyPersistentCache, true).toBool();
  recent_files = settings.value(kKeyRecentFiles).toStringList();
  while (recent_files.size() > kMaxRecentFiles) {
    recent_files.removeLast();
  }
  root_dir = settings.value(kKeyRootDir).toString();
  last_file = settings.value(kKeyLastFile).toString();
  bookmarks_json = settings.value(kKeyBookmarks).toString();
  positions_json = settings.value(kKeyPositions).toString();
}

void MdViewerSettings::Save() const {
  QSettings settings(WritableSettingsPath(), QSettings::IniFormat);
  SetIniUtf8(settings);
  if (!window_geometry.isEmpty()) {
    settings.setValue(kKeyGeometry, window_geometry);
  }
  if (!dock_state.isEmpty()) {
    settings.setValue(kKeyDockState, dock_state);
  }
  settings.setValue(kKeyDarkTheme, dark_theme);
  settings.setValue(kKeyWindowMaterial, static_cast<int>(window_material));
  settings.setValue(kKeyWindowBlur, window_material != WindowMaterial::kNone);
  settings.setValue(kKeyReadingStyle, static_cast<int>(reading_style));
  settings.setValue(kKeyTransparentReading, transparent_reading);
  settings.setValue(kKeyReadingFontSize, reading_font_size);
  settings.setValue(kKeyReadingLineHeight, reading_line_height_percent);
  settings.setValue(kKeyReadingMeasure, reading_measure_px);
  settings.setValue(kKeyCodeLineNumbers, code_line_numbers);
  settings.setValue(kKeyAdmonitionMode, static_cast<int>(admonition_mode));
  settings.setValue(kKeyMermaidBackend, static_cast<int>(mermaid_backend));
  settings.setValue(kKeyFormula, formula_rendering);
  settings.setValue(kKeyMermaid, mermaid_rendering);
  settings.setValue(kKeyPersistentCache, persistent_cache);
  settings.setValue(kKeyRecentFiles, recent_files);
  settings.setValue(kKeyRootDir, root_dir);
  settings.setValue(kKeyLastFile, last_file);
  settings.setValue(kKeyBookmarks, bookmarks_json);
  settings.setValue(kKeyPositions, positions_json);
  settings.sync();
  WarnWriteFailed(settings);
}

void MdViewerSettings::SaveRecent() const {
  QSettings settings(WritableSettingsPath(), QSettings::IniFormat);
  SetIniUtf8(settings);
  settings.setValue(kKeyRecentFiles, recent_files);
  settings.sync();
  WarnWriteFailed(settings);
}

void MdViewerSettings::SaveSession() const {
  QSettings settings(WritableSettingsPath(), QSettings::IniFormat);
  SetIniUtf8(settings);
  settings.setValue(kKeyRootDir, root_dir);
  settings.setValue(kKeyLastFile, last_file);
  settings.sync();
  WarnWriteFailed(settings);
}

void MdViewerSettings::SaveBookmarks() const {
  QSettings settings(WritableSettingsPath(), QSettings::IniFormat);
  SetIniUtf8(settings);
  settings.setValue(kKeyBookmarks, bookmarks_json);
  settings.sync();
  WarnWriteFailed(settings);
}

void MdViewerSettings::SavePositions() const {
  QSettings settings(WritableSettingsPath(), QSettings::IniFormat);
  SetIniUtf8(settings);
  settings.setValue(kKeyPositions, positions_json);
  settings.sync();
  WarnWriteFailed(settings);
}

void MdViewerSettings::AddRecentFile(const QString& path) {
  const QString absolute_path = QFileInfo(path).absoluteFilePath();
  recent_files.removeAll(absolute_path);
  recent_files.prepend(absolute_path);
  while (recent_files.size() > kMaxRecentFiles) {
    recent_files.removeLast();
  }
  SaveRecent();
}
