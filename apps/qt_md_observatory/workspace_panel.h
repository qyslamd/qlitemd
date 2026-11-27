#ifndef FILE_APPS_QT_MD_OBSERVATORY_WORKSPACE_PANEL_H_
#define FILE_APPS_QT_MD_OBSERVATORY_WORKSPACE_PANEL_H_

#include <QVector>
#include <QWidget>

class QLabel;
class QTreeWidget;
class QTreeWidgetItem;
class WorkspaceModel;

// Left rail: workspace root, aggregate progress and the grouped document list.
class WorkspacePanel : public QWidget {
  Q_OBJECT

 public:
  explicit WorkspacePanel(QWidget* parent = nullptr);

  void SetModel(WorkspaceModel* model);
  void SetCurrentIndex(int index);

 signals:
  void documentActivated(int index);

 private:
  void Rebuild();
  void OnItemActivated(QTreeWidgetItem* item);
  void UpdateProgress(int index);
  void UpdateOverall();

  WorkspaceModel* model_ = nullptr;
  QTreeWidget* tree_ = nullptr;
  bool updating_ = false;
  QLabel* root_label_ = nullptr;
  QLabel* overall_label_ = nullptr;
  QVector<QTreeWidgetItem*> document_items_;
};

#endif  // FILE_APPS_QT_MD_OBSERVATORY_WORKSPACE_PANEL_H_
