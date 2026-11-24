#include "session_controller.h"

#include "md_viewer_settings.h"
#include "tab_session_codec.h"

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

void SessionController::SetSidebarVisible(bool visible) {
  settings_->sidebar_visible = visible;
}

void SessionController::SetSidebarView(SideView view) {
  settings_->sidebar_view = static_cast<int>(view);
  settings_->sidebar_visible = true;
}

void SessionController::SetOutlineVisible(bool visible) {
  settings_->outline_visible = visible;
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
