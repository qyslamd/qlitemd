#include "settings_dialog.h"

#include <QHBoxLayout>
#include <QPushButton>
#include <QVBoxLayout>

SettingsDialog::SettingsDialog(QWidget* content, QWidget* parent)
    : QDialog(parent) {
  setObjectName(QStringLiteral("settingsDialog"));
  setWindowTitle(QStringLiteral("设置"));
  setModal(false);
  resize(780, 560);
  setMinimumSize(640, 420);

  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);
  if (content != nullptr) {
    layout->addWidget(content, 1);
  }

  // 即时生效，没有“应用/取消”；关闭只是隐藏，下次打开保留选中分类
  auto* bottom = new QHBoxLayout();
  bottom->setContentsMargins(16, 8, 16, 12);
  bottom->addStretch();
  auto* close_button = new QPushButton(QStringLiteral("关闭"), this);
  close_button->setObjectName(QStringLiteral("settingsCloseButton"));
  connect(close_button, &QPushButton::clicked, this, &QDialog::hide);
  bottom->addWidget(close_button);
  layout->addLayout(bottom);
}
