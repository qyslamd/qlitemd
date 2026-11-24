#include "tab_session_codec.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>

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

bool TabSessionCodec::Remove(int index) {
  if (index <= 0 || index >= paths_.size()) {
    return false;
  }
  paths_.removeAt(index);
  if (index < active_) {
    --active_;
  } else if (index == active_) {
    // 关掉当前标签落在右侧邻居；关的是最后一个就落回前一个
    active_ = qMin(index, paths_.size() - 1);
  }
  return true;
}

bool TabSessionCodec::SetActive(int index) {
  if (index < 0 || index >= paths_.size()) {
    return false;
  }
  active_ = index;
  return true;
}

bool TabSessionCodec::Move(int from, int to) {
  if (from <= 0 || to <= 0 || from >= paths_.size() || to >= paths_.size()) {
    return false;
  }
  if (from == to) {
    return true;
  }
  const QString active_path = paths_.at(active_);
  const QString moved = paths_.takeAt(from);
  paths_.insert(to, moved);
  active_ = paths_.indexOf(active_path);
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
