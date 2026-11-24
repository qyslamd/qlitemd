#include "bookmark_controller.h"

#include "md_viewer_settings.h"

BookmarkController::BookmarkController(MdViewerSettings* settings)
    : settings_(settings) {}

void BookmarkController::Load() {
  store_ = BookmarkStore::FromJson(settings_->bookmarks_json);
}

bool BookmarkController::Contains(const QString& path) const {
  return store_.Contains(path);
}

bool BookmarkController::Toggle(const QString& path, const QString& root) {
  return store_.Toggle(path, root);
}

void BookmarkController::Remove(const QString& path) { store_.Remove(path); }

const QVector<Bookmark>& BookmarkController::items() const {
  return store_.items();
}

void BookmarkController::Save() const {
  settings_->bookmarks_json = store_.ToJson();
  settings_->SaveBookmarks();
}
