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
constexpr char kKeyWindowBlur[] = "appearance/window_blur";
constexpr char kKeyCodeLineNumbers[] = "render/code_line_numbers";
constexpr char kKeyFormula[] = "render/formula";
constexpr char kKeyMermaid[] = "render/mermaid";
constexpr char kKeyPersistentCache[] = "render/persistent_cache";
constexpr char kKeyRecentFiles[] = "recent/files";

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
  window_blur = settings.value(kKeyWindowBlur, true).toBool();
  code_line_numbers = settings.value(kKeyCodeLineNumbers, false).toBool();
  formula_rendering = settings.value(kKeyFormula, true).toBool();
  mermaid_rendering = settings.value(kKeyMermaid, true).toBool();
  persistent_cache = settings.value(kKeyPersistentCache, true).toBool();
  recent_files = settings.value(kKeyRecentFiles).toStringList();
  while (recent_files.size() > kMaxRecentFiles) {
    recent_files.removeLast();
  }
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
  settings.setValue(kKeyWindowBlur, window_blur);
  settings.setValue(kKeyCodeLineNumbers, code_line_numbers);
  settings.setValue(kKeyFormula, formula_rendering);
  settings.setValue(kKeyMermaid, mermaid_rendering);
  settings.setValue(kKeyPersistentCache, persistent_cache);
  settings.setValue(kKeyRecentFiles, recent_files);
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

void MdViewerSettings::AddRecentFile(const QString& path) {
  const QString absolute_path = QFileInfo(path).absoluteFilePath();
  recent_files.removeAll(absolute_path);
  recent_files.prepend(absolute_path);
  while (recent_files.size() > kMaxRecentFiles) {
    recent_files.removeLast();
  }
  SaveRecent();
}
