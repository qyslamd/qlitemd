#include "workspace_model.h"

#include <algorithm>

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QSettings>
#include <QStringList>
#include <QUrl>

namespace {

const QStringList& IgnoredSegments() {
  static const QStringList kIgnored = {
      QStringLiteral("build"),  QStringLiteral("node_modules"),
      QStringLiteral("_build"), QStringLiteral("dist"),
      QStringLiteral("out"),
  };
  return kIgnored;
}

bool IsIgnoredSegment(const QString& segment) {
  if (segment.startsWith(QLatin1Char('.'))) {
    return true;
  }
  return IgnoredSegments().contains(segment, Qt::CaseInsensitive);
}

bool IsMarkdownFile(const QFileInfo& info) {
  if (info.fileName().startsWith(QLatin1Char('.'))) {
    return false;
  }
  const QString suffix = info.suffix().toLower();
  return suffix == QStringLiteral("md") || suffix == QStringLiteral("markdown");
}

}  // namespace

WorkspaceModel::WorkspaceModel(QObject* parent) : QObject(parent) {}

WorkspaceModel::~WorkspaceModel() { Flush(); }

bool WorkspaceModel::Load(const QString& root) {
  QDir dir(root);
  if (!dir.exists()) {
    return false;
  }
  root_ = dir.absolutePath();
  documents_.clear();

  QDirIterator it(root_, QDir::Files | QDir::NoDotAndDotDot,
                  QDirIterator::Subdirectories);
  while (it.hasNext()) {
    const QString path = it.next();
    const QFileInfo info(path);
    if (!IsMarkdownFile(info)) {
      continue;
    }
    const QString relative = QDir(root_).relativeFilePath(path);
    if (relative.startsWith(QStringLiteral(".."))) {
      continue;
    }
    const QStringList segments =
        relative.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    bool ignored = false;
    for (int i = 0; i + 1 < segments.size(); ++i) {
      if (IsIgnoredSegment(segments[i])) {
        ignored = true;
        break;
      }
    }
    if (ignored) {
      continue;
    }

    WorkspaceDocument document;
    document.absolute_path = QDir::cleanPath(path);
    document.relative_path = relative;
    document.title = info.completeBaseName();
    document.group =
        segments.size() > 1 ? segments.front() : QStringLiteral("ROOT");
    documents_.push_back(std::move(document));
  }

  std::sort(documents_.begin(), documents_.end(),
            [](const WorkspaceDocument& a, const WorkspaceDocument& b) {
              // Root-level documents first; named groups follow alphabetically.
              const bool a_root = a.group == QStringLiteral("ROOT");
              const bool b_root = b.group == QStringLiteral("ROOT");
              if (a_root != b_root) {
                return a_root;
              }
              const int group_compare =
                  QString::compare(a.group, b.group, Qt::CaseInsensitive);
              if (group_compare != 0) {
                return group_compare < 0;
              }
              return QString::compare(a.relative_path, b.relative_path,
                                      Qt::CaseInsensitive) < 0;
            });

  LoadPositions();
  emit changed();
  return true;
}

void WorkspaceModel::Clear() {
  root_.clear();
  documents_.clear();
  emit changed();
}

int WorkspaceModel::indexOf(const QString& absolute_path) const {
  const QString normalized = QDir::cleanPath(absolute_path);
  for (int i = 0; i < documents_.size(); ++i) {
    if (documents_[i].absolute_path == normalized) {
      return i;
    }
  }
  return -1;
}

int WorkspaceModel::overallProgress() const {
  if (documents_.isEmpty()) {
    return 0;
  }
  int sum = 0;
  for (const WorkspaceDocument& document : documents_) {
    sum += document.progress;
  }
  return (sum + documents_.size() / 2) / documents_.size();
}

void WorkspaceModel::SetPosition(int index, int scroll, int progress,
                                 bool persist) {
  if (index < 0 || index >= documents_.size()) {
    return;
  }
  WorkspaceDocument& document = documents_[index];
  const int clamped_scroll = qMax(0, scroll);
  const int clamped_progress = qBound(0, progress, 100);
  if (document.scroll == clamped_scroll &&
      document.progress == clamped_progress) {
    return;
  }
  document.scroll = clamped_scroll;
  document.progress = clamped_progress;
  if (persist) {
    const QString key = PositionKey(document);
    settings()->setValue(key + QStringLiteral("/scroll"), document.scroll);
    settings()->setValue(key + QStringLiteral("/progress"), document.progress);
    settings()->sync();
  }
  emit progressChanged(index);
}

void WorkspaceModel::Flush() {
  if (settings_ != nullptr) {
    settings_->sync();
  }
}

QString WorkspaceModel::WorkspaceKey() const {
  const QByteArray hash =
      QCryptographicHash::hash(root_.toUtf8(), QCryptographicHash::Sha1);
  return QString::fromLatin1(hash.toHex());
}

QString WorkspaceModel::PositionKey(const WorkspaceDocument& document) const {
  const QString encoded =
      QString::fromLatin1(QUrl::toPercentEncoding(document.relative_path));
  return QStringLiteral("workspaces/") + WorkspaceKey() + QLatin1Char('/') +
         encoded;
}

void WorkspaceModel::LoadPositions() {
  for (WorkspaceDocument& document : documents_) {
    const QString key = PositionKey(document);
    document.scroll =
        settings()->value(key + QStringLiteral("/scroll"), 0).toInt();
    document.progress = qBound(
        0, settings()->value(key + QStringLiteral("/progress"), 0).toInt(),
        100);
  }
}

QSettings* WorkspaceModel::settings() {
  if (settings_ == nullptr) {
    settings_ = std::make_unique<QSettings>();
  }
  return settings_.get();
}
