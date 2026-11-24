#include "reading_position_controller.h"

#include "md_viewer_settings.h"

ReadingPositionController::ReadingPositionController(MdViewerSettings* settings)
    : settings_(settings) {}

void ReadingPositionController::Load() {
  store_ = ReadingPositionStore::FromJson(settings_->positions_json);
}

int ReadingPositionController::ScrollFor(const QString& path) const {
  return store_.ScrollFor(path);
}

void ReadingPositionController::Remember(const QString& path, int scroll,
                                         qint64 now_seconds) {
  store_.Update(path, scroll, now_seconds);
  Save();
}

void ReadingPositionController::Save() const {
  settings_->positions_json = store_.ToJson();
  settings_->SavePositions();
}
