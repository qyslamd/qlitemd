#ifndef FILE_APPS_QT_MD_OBSERVATORY_WORKSPACE_MODEL_H_
#define FILE_APPS_QT_MD_OBSERVATORY_WORKSPACE_MODEL_H_

#include <memory>

#include <QObject>
#include <QString>
#include <QVector>

class QSettings;

// One Markdown document discovered under the workspace root.
struct WorkspaceDocument {
  QString absolute_path;
  QString relative_path;
  QString title;
  QString group;
  // Last committed vertical scroll in litehtml document coordinates.
  int scroll = 0;
  // 0..100, used by the workspace list and the workspace aggregate.
  int progress = 0;
};

// Scans a directory for Markdown, groups by first path segment and persists
// per-document reading positions. Pure QtCore so it can be tested headless.
class WorkspaceModel : public QObject {
  Q_OBJECT

 public:
  explicit WorkspaceModel(QObject* parent = nullptr);
  ~WorkspaceModel() override;

  bool Load(const QString& root);
  void Clear();

  const QString& root() const { return root_; }
  const QVector<WorkspaceDocument>& documents() const { return documents_; }
  int count() const { return documents_.size(); }
  int indexOf(const QString& absolute_path) const;
  int overallProgress() const;

  // Updates the in-memory entry and, when persist is true, writes QSettings.
  void SetPosition(int index, int scroll, int progress, bool persist);

  void Flush();

 signals:
  void changed();
  void progressChanged(int index);

 private:
  struct Position {
    int scroll = 0;
    int progress = 0;
  };

  QString WorkspaceKey() const;
  QString PositionKey(const WorkspaceDocument& document) const;
  void LoadPositions();
  QSettings* settings();

  QString root_;
  QVector<WorkspaceDocument> documents_;
  std::unique_ptr<QSettings> settings_;
};

#endif  // FILE_APPS_QT_MD_OBSERVATORY_WORKSPACE_MODEL_H_
