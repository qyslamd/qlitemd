#include "session_controller.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>

#include "md_viewer_settings.h"

TabSessionCodec::TabSessionCodec() { paths_.append(QString()); }

int TabSessionCodec::Add(const QString& path) {
  if (path.isEmpty()) {
    return 0;
  }
  const int existing = paths_.indexOf(path);
  if (existing >= 0) {
    return existing;
  }
  paths_.append(path);
  return paths_.size() - 1;
}

bool TabSessionCodec::SetActive(int index) {
  if (index < 0 || index >= paths_.size()) {
    return false;
  }
  active_ = index;
  return true;
}

QString TabSessionCodec::path_at(int index) const {
  if (index < 0 || index >= paths_.size()) {
    return QString();
  }
  return paths_.at(index);
}

QByteArray TabSessionCodec::Serialize() const {
  QJsonArray tabs;
  for (const QString& path : paths_) {
    tabs.append(path);
  }
  QJsonObject object;
  object.insert(QStringLiteral("tabs"), tabs);
  object.insert(QStringLiteral("active"), active_);
  return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

bool TabSessionCodec::Restore(const QByteArray& data) {
  QJsonParseError error{};
  const QJsonDocument document = QJsonDocument::fromJson(data, &error);
  if (error.error != QJsonParseError::NoError || !document.isObject()) {
    return false;
  }
  const QJsonValue tabs_value = document.object().value(QStringLiteral("tabs"));
  if (!tabs_value.isArray()) {
    return false;
  }

  QStringList restored;
  restored.append(QString());
  for (const QJsonValue& value : tabs_value.toArray()) {
    const QString path = value.toString();
    if (path.isEmpty() || restored.contains(path)) {
      continue;
    }
    restored.append(path);
  }

  int restored_active =
      document.object().value(QStringLiteral("active")).toInt(0);
  if (restored_active < 0) {
    restored_active = 0;
  }
  if (restored_active >= restored.size()) {
    restored_active = restored.size() - 1;
  }

  paths_ = restored;
  active_ = restored_active;
  return true;
}

SessionController::SessionController(MdViewerSettings* settings,
                                     bool session_owner)
    : settings_(settings), owner_(session_owner) {}

bool SessionController::RestoreRequested() const {
  return !settings_->workspace_closed;
}

QByteArray SessionController::SavedGeometry() const {
  return settings_->window_geometry;
}

ShellLayoutState SessionController::SavedShellLayout() const {
  ShellLayoutState layout;
  layout.view = SideViewFromInt(settings_->sidebar_view);
  layout.sidebar_visible = settings_->sidebar_visible;
  layout.sidebar_width = settings_->sidebar_width;
  layout.outline_width = settings_->outline_width;
  layout.outline_visible = settings_->outline_visible;
  return layout;
}

QVector<QString> SessionController::SavedTabPaths(int* active) const {
  QVector<QString> paths;
  if (active != nullptr) {
    *active = 0;
  }
  TabSessionCodec codec;
  if (!codec.Restore(settings_->open_tabs_json) || codec.count() <= 1) {
    return paths;
  }
  for (int i = 0; i < codec.count(); ++i) {
    paths.append(codec.path_at(i));
  }
  if (active != nullptr) {
    *active = codec.active();
  }
  return paths;
}

QString SessionController::SavedRoot() const { return settings_->root_dir; }

QString SessionController::SavedLastFile() const {
  return settings_->last_file;
}

void SessionController::SaveGeometry(const QByteArray& geometry) {
  if (!owner_) {
    return;
  }
  settings_->window_geometry = geometry;
}

void SessionController::SaveShellLayout(const ShellLayoutState& layout) {
  if (!owner_) {
    return;
  }
  settings_->sidebar_view = static_cast<int>(layout.view);
  settings_->sidebar_visible = layout.sidebar_visible;
  settings_->sidebar_width = layout.sidebar_width;
  settings_->outline_width = layout.outline_width;
  settings_->outline_visible = layout.outline_visible;
}

void SessionController::SaveTabs(const QVector<QString>& paths, int active) {
  if (!owner_ || restoring_) {
    return;
  }
  TabSessionCodec codec;
  for (const QString& path : paths) {
    codec.Add(path);
  }
  if (active >= 0 && active < codec.count()) {
    codec.SetActive(active);
  }
  settings_->open_tabs_json = codec.Serialize();
  settings_->last_file =
      active >= 0 && active < paths.size() ? paths.at(active) : QString();
  // 恢复结束时需要强制写回一次，是否跳过由调用方决定
  settings_->SaveSession();
}

void SessionController::SetRoot(const QString& root) {
  if (!owner_) {
    return;
  }
  settings_->root_dir = root;
  settings_->workspace_closed = false;
}

void SessionController::MarkWorkspaceOpened(const QString& path) {
  if (!owner_) {
    return;
  }
  settings_->workspace_closed = false;
  settings_->last_file = path;
}

void SessionController::ClearWorkspace() {
  if (!owner_) {
    return;
  }
  settings_->root_dir.clear();
  settings_->last_file.clear();
  settings_->open_tabs_json.clear();
  settings_->workspace_closed = true;
  SaveSessionIfAllowed();
}

void SessionController::SaveAll() const {
  if (!owner_) {
    return;
  }
  settings_->Save();
}

void SessionController::SaveSessionIfAllowed() const {
  if (!owner_ || restoring_) {
    return;
  }
  settings_->SaveSession();
}
