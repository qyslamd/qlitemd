#include "workspace_panel.h"

#include <QFont>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

#include "workspace_model.h"

WorkspacePanel::WorkspacePanel(QWidget* parent) : QWidget(parent) {
  setObjectName(QStringLiteral("workspacePanel"));

  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(14, 14, 12, 12);
  layout->setSpacing(4);

  auto* title_row = new QHBoxLayout();
  auto* title = new QLabel(tr("WORKSPACE"), this);
  title->setObjectName(QStringLiteral("panelTitle"));
  overall_label_ = new QLabel(this);
  overall_label_->setObjectName(QStringLiteral("workspaceOverall"));
  overall_label_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
  title_row->addWidget(title);
  title_row->addStretch(1);
  title_row->addWidget(overall_label_);
  layout->addLayout(title_row);

  root_label_ = new QLabel(this);
  root_label_->setObjectName(QStringLiteral("muted"));
  root_label_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
  layout->addWidget(root_label_);
  layout->addSpacing(8);

  tree_ = new QTreeWidget(this);
  tree_->setObjectName(QStringLiteral("workspaceTree"));
  tree_->setHeaderHidden(true);
  tree_->setColumnCount(2);
  tree_->setRootIsDecorated(true);
  tree_->setIndentation(12);
  tree_->setUniformRowHeights(true);
  tree_->setAnimated(false);
  tree_->setExpandsOnDoubleClick(false);
  tree_->header()->setStretchLastSection(false);
  tree_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
  tree_->header()->setSectionResizeMode(1, QHeaderView::Fixed);
  tree_->setColumnWidth(1, 44);
  layout->addWidget(tree_, 1);

  // currentItemChanged fires for mouse, keyboard and accessibility selection.
  // Rebuild/SetCurrentIndex guard it so programmatic updates do not open a
  // document.
  connect(tree_, &QTreeWidget::currentItemChanged, this,
          [this](QTreeWidgetItem* current, QTreeWidgetItem*) {
            if (!updating_) {
              OnItemActivated(current);
            }
          });
}

void WorkspacePanel::SetModel(WorkspaceModel* model) {
  if (model_ != nullptr) {
    disconnect(model_, nullptr, this, nullptr);
  }
  model_ = model;
  if (model_ != nullptr) {
    connect(model_, &WorkspaceModel::changed, this, &WorkspacePanel::Rebuild);
    connect(model_, &WorkspaceModel::progressChanged, this,
            &WorkspacePanel::UpdateProgress);
  }
  Rebuild();
}

void WorkspacePanel::Rebuild() {
  updating_ = true;
  document_items_.clear();
  tree_->clear();
  if (model_ == nullptr) {
    root_label_->clear();
    overall_label_->clear();
    updating_ = false;
    return;
  }

  root_label_->setText(model_->root());
  root_label_->setToolTip(model_->root());

  QTreeWidgetItem* group_item = nullptr;
  QString current_group;
  const QVector<WorkspaceDocument>& documents = model_->documents();
  for (int i = 0; i < documents.size(); ++i) {
    const WorkspaceDocument& document = documents[i];
    if (group_item == nullptr || document.group != current_group) {
      current_group = document.group;
      group_item = new QTreeWidgetItem(tree_);
      group_item->setText(0, document.group.toUpper());
      group_item->setFlags(Qt::ItemIsEnabled);
      QFont group_font = group_item->font(0);
      group_font.setBold(true);
      group_font.setPointSizeF(group_font.pointSizeF() - 0.5);
      group_item->setFont(0, group_font);
      group_item->setForeground(
          0, palette().color(QPalette::Disabled, QPalette::WindowText));
    }

    auto* item = new QTreeWidgetItem(group_item);
    item->setText(0, document.title);
    item->setText(1, QStringLiteral("%1%").arg(document.progress));
    item->setTextAlignment(1, Qt::AlignRight | Qt::AlignVCenter);
    item->setToolTip(0, document.relative_path);
    item->setData(0, Qt::UserRole, i);
    item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    document_items_.push_back(item);
  }
  tree_->expandAll();
  UpdateOverall();
  updating_ = false;
}

void WorkspacePanel::SetCurrentIndex(int index) {
  updating_ = true;
  if (index < 0 || index >= document_items_.size()) {
    tree_->clearSelection();
    updating_ = false;
    return;
  }
  tree_->setCurrentItem(document_items_[index]);
  tree_->scrollToItem(document_items_[index]);
  updating_ = false;
}

void WorkspacePanel::OnItemActivated(QTreeWidgetItem* item) {
  if (item == nullptr) {
    return;
  }
  const QVariant value = item->data(0, Qt::UserRole);
  if (!value.isValid()) {
    return;
  }
  emit documentActivated(value.toInt());
}

void WorkspacePanel::UpdateProgress(int index) {
  if (model_ == nullptr || index < 0 || index >= document_items_.size()) {
    return;
  }
  const WorkspaceDocument& document = model_->documents()[index];
  document_items_[index]->setText(1,
                                  QStringLiteral("%1%").arg(document.progress));
  UpdateOverall();
}

void WorkspacePanel::UpdateOverall() {
  if (model_ == nullptr) {
    overall_label_->clear();
    return;
  }
  overall_label_->setText(QStringLiteral("%1%").arg(model_->overallProgress()));
}
