#include "settings_dialog.h"

#include <QVBoxLayout>

SettingsDialog::SettingsDialog(QWidget* content, QWidget* parent)
    : QDialog(parent) {
  setObjectName(QStringLiteral("settingsDialog"));
  setWindowTitle(QStringLiteral("设置"));
  setModal(false);
  resize(420, 680);
  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);
  if (content != nullptr) {
    layout->addWidget(content);
  }
}
