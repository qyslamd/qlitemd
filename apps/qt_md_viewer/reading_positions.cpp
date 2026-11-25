#include "reading_positions.h"

#include <algorithm>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>

#include "md_viewer_settings.h"

ReadingPositionStore ReadingPositionStore::FromJson(const QString& json,
                                                    int capacity) {
  ReadingPositionStore store;
  store.capacity_ = std::max(1, capacity);
  if (json.isEmpty()) {
    return store;
  }
  QJsonParseError error;
  const QJsonDocument document = QJsonDocument::fromJson(json.toUtf8(), &error);
  if (error.error != QJsonParseError::NoError || !document.isArray()) {
    return store;
  }
  for (const QJsonValue& value : document.array()) {
    if (!value.isObject()) {
      continue;
    }
    const QJsonObject object = value.toObject();
    const QString path = object.value(QStringLiteral("path")).toString();
    if (path.isEmpty()) {
      continue;
    }
    ReadingPosition position;
    position.path = path;
    position.scroll = object.value(QStringLiteral("scroll")).toInt();
    position.touched =
        static_cast<qint64>(object.value(QStringLiteral("touched")).toDouble());
    store.items_.append(position);
    if (store.items_.size() >= store.capacity_) {
      break;
    }
  }
  return store;
}

QString ReadingPositionStore::ToJson() const {
  QJsonArray array;
  for (const ReadingPosition& position : items_) {
    QJsonObject object;
    object.insert(QStringLiteral("path"), position.path);
    object.insert(QStringLiteral("scroll"), position.scroll);
    object.insert(QStringLiteral("touched"),
                  static_cast<double>(position.touched));
    array.append(object);
  }
  return QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact));
}

int ReadingPositionStore::ScrollFor(const QString& path) const {
  for (const ReadingPosition& position : items_) {
    if (position.path == path) {
      return position.scroll;
    }
  }
  return -1;
}

void ReadingPositionStore::Update(const QString& path, int scroll,
                                  qint64 now_seconds) {
  for (int i = items_.size() - 1; i >= 0; --i) {
    if (items_.at(i).path == path) {
      items_.removeAt(i);
    }
  }
  items_.prepend({path, scroll, now_seconds});
  while (items_.size() > capacity_) {
    items_.removeLast();
  }
}

int ReadingPositionStore::size() const { return items_.size(); }

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
