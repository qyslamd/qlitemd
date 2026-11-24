#include "welcome_bar.h"

#include <QDir>
#include <QFileInfo>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QPalette>
#include <QResizeEvent>
#include <QSizePolicy>
#include <QToolButton>

#include "theme_icons.h"

namespace {

QToolButton* MakeIconButton(QWidget* parent, const QString& name,
                            const QString& tooltip) {
  auto* button = new QToolButton(parent);
  button->setObjectName(name);
  button->setAutoRaise(true);
  button->setToolTip(tooltip);
  button->setToolButtonStyle(Qt::ToolButtonIconOnly);
  button->setFixedSize(24, 24);
  return button;
}

}  // namespace

WelcomeBar::WelcomeBar(QWidget* parent) : QWidget(parent) {
  setObjectName(QStringLiteral("welcomeBar"));
  setFixedHeight(32);
  auto* layout = new QHBoxLayout(this);
  layout->setContentsMargins(8, 0, 8, 0);
  layout->setSpacing(6);

  auto* mark = new QLabel(this);
  mark->setObjectName(QStringLiteral("welcomeMark"));
  mark->setPixmap(
      QIcon(QStringLiteral(":/resources/svg/icon.svg")).pixmap(16, 16));
  mark->setFixedSize(16, 16);
  layout->addWidget(mark);

  auto* title = new QLabel(QStringLiteral("欢迎"), this);
  title->setObjectName(QStringLiteral("welcomeTitle"));
  layout->addWidget(title);

  workspace_label_ = new QLabel(this);
  workspace_label_->setObjectName(QStringLiteral("welcomeWorkspaceLabel"));
  workspace_label_->setForegroundRole(QPalette::PlaceholderText);
  workspace_label_->setSizePolicy(QSizePolicy::Expanding,
                                  QSizePolicy::Preferred);
  workspace_label_->setTextInteractionFlags(Qt::NoTextInteraction);
  layout->addWidget(workspace_label_, 1);

  quick_open_button_ = MakeIconButton(this, QStringLiteral("welcomeQuickOpen"),
                                      QStringLiteral("快速跳转（Ctrl+P）"));
  layout->addWidget(quick_open_button_);
  layout->addWidget(quick_open_button_);

  connect(quick_open_button_, &QToolButton::clicked, this,
          &WelcomeBar::quickOpenRequested);

  SetThemeDark(false);
  SetWorkspaceName(QString());
}

void WelcomeBar::SetWorkspaceName(const QString& root) {
  if (root.isEmpty()) {
    workspace_display_ = QStringLiteral("未打开工作区");
    workspace_label_->setToolTip(QString());
  } else {
    workspace_display_ = QFileInfo(root).fileName();
    if (workspace_display_.isEmpty()) {
      workspace_display_ = root;
    }
    workspace_label_->setToolTip(QDir::toNativeSeparators(root));
  }
  UpdateWorkspaceText();
}

void WelcomeBar::SetThemeDark(bool dark) {
  dark_ = dark;
  quick_open_button_->setIcon(ThemeIcon(QStringLiteral("search"), dark_));
}

void WelcomeBar::resizeEvent(QResizeEvent* event) {
  QWidget::resizeEvent(event);
  // 窄窗只留欢迎标识与工作区名，快速跳转让位
  quick_open_button_->setVisible(width() >= 480);
  UpdateWorkspaceText();
}

void WelcomeBar::UpdateWorkspaceText() {
  if (workspace_label_ == nullptr) {
    return;
  }
  // 还没布局时宽度不可信，先放全文；resizeEvent 里再按真实宽度省略
  const int available = workspace_label_->width();
  if (!isVisible() || available <= 20) {
    workspace_label_->setText(workspace_display_);
    return;
  }
  workspace_label_->setText(
      QFontMetrics(workspace_label_->font())
          .elidedText(workspace_display_, Qt::ElideMiddle, available));
}
