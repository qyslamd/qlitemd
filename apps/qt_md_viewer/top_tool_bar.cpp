#include "top_tool_bar.h"

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QPaintEvent>
#include <QPainter>
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

TopToolBar::TopToolBar(QWidget* parent) : QToolBar(parent) {
  setObjectName(QStringLiteral("topToolBar"));
  setAllowedAreas(Qt::TopToolBarArea);
  setContextMenuPolicy(Qt::PreventContextMenu);
  ResetInvariants();
  // 工具栏自己的布局默认带 style 给的边框内边距，这里清掉，
  // 让内容控件贴满 32px 的条
  setContentsMargins(0, 0, 0, 0);
  if (layout() != nullptr) {
    layout()->setContentsMargins(0, 0, 0, 0);
  }
}

void TopToolBar::ResetInvariants() {
  setMovable(false);
  setFloatable(false);
  setFixedHeight(32);
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  show();
}

DocumentBar::DocumentBar(QWidget* parent) : QWidget(parent) {
  setObjectName(QStringLiteral("documentBar"));
  setFixedHeight(32);
  auto* layout = new QHBoxLayout(this);
  layout->setContentsMargins(8, 0, 8, 0);
  layout->setSpacing(4);

  back_button_ = MakeIconButton(this, QStringLiteral("docBack"),
                                QStringLiteral("后退（Ctrl+Alt+←）"));
  forward_button_ = MakeIconButton(this, QStringLiteral("docForward"),
                                   QStringLiteral("前进（Ctrl+Alt+→）"));
  star_button_ = MakeIconButton(this, QStringLiteral("docStar"),
                                QStringLiteral("收藏当前文档（Ctrl+D）"));
  star_button_->setCheckable(true);
  layout->addWidget(back_button_);
  layout->addWidget(forward_button_);
  layout->addWidget(star_button_);
  layout->addSpacing(4);

  path_label_ = new QLabel(this);
  path_label_->setObjectName(QStringLiteral("docPathLabel"));
  path_label_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
  path_label_->setTextInteractionFlags(Qt::NoTextInteraction);
  layout->addWidget(path_label_, 1);

  copy_button_ = new QToolButton(this);
  copy_button_->setObjectName(QStringLiteral("docCopyPath"));
  copy_button_->setAutoRaise(true);
  copy_button_->setText(QStringLiteral("复制"));
  copy_button_->setToolButtonStyle(Qt::ToolButtonTextOnly);
  copy_button_->setToolTip(QStringLiteral("复制当前文档路径"));
  layout->addWidget(copy_button_);

  find_edit_ = new QLineEdit(this);
  find_edit_->setObjectName(QStringLiteral("docFindEdit"));
  find_edit_->setPlaceholderText(QStringLiteral("在当前文档中查找（Ctrl+F）"));
  find_edit_->setClearButtonEnabled(true);
  find_edit_->setFixedWidth(240);
  layout->addWidget(find_edit_);

  appearance_button_ =
      MakeIconButton(this, QStringLiteral("docAppearance"),
                     QStringLiteral("阅读外观（字号、行距、栏宽）"));
  layout->addWidget(appearance_button_);

  zoom_group_ = new QWidget(this);
  zoom_group_->setObjectName(QStringLiteral("docZoomGroup"));
  auto* zoom_layout = new QHBoxLayout(zoom_group_);
  zoom_layout->setContentsMargins(0, 0, 0, 0);
  zoom_layout->setSpacing(2);
  zoom_out_button_ = MakeIconButton(zoom_group_, QStringLiteral("docZoomOut"),
                                    QStringLiteral("缩小（Ctrl+-）"));
  zoom_reset_button_ = new QToolButton(zoom_group_);
  zoom_reset_button_->setObjectName(QStringLiteral("docZoomReset"));
  zoom_reset_button_->setAutoRaise(true);
  zoom_reset_button_->setText(QStringLiteral("100%"));
  zoom_reset_button_->setToolButtonStyle(Qt::ToolButtonTextOnly);
  zoom_reset_button_->setToolTip(QStringLiteral("重置缩放（Ctrl+0）"));
  zoom_reset_button_->setMinimumWidth(46);
  zoom_in_button_ = MakeIconButton(zoom_group_, QStringLiteral("docZoomIn"),
                                   QStringLiteral("放大（Ctrl+=）"));
  zoom_layout->addWidget(zoom_out_button_);
  zoom_layout->addWidget(zoom_reset_button_);
  zoom_layout->addWidget(zoom_in_button_);
  layout->addWidget(zoom_group_);

  SetThemeDark(false);
  UpdateChildVisibility();
}

void DocumentBar::SetPath(const QString& display_path, const QString& tooltip) {
  display_path_ = display_path;
  path_label_->setToolTip(tooltip);
  UpdatePathElision();
}

void DocumentBar::SetThemeDark(bool dark) {
  dark_ = dark;
  back_button_->setIcon(ThemeIcon(QStringLiteral("back"), dark_));
  forward_button_->setIcon(ThemeIcon(QStringLiteral("forward"), dark_));
  appearance_button_->setIcon(ThemeIcon(QStringLiteral("appearance"), dark_));
  zoom_out_button_->setIcon(ThemeIcon(QStringLiteral("zoom-out"), dark_));
  zoom_in_button_->setIcon(ThemeIcon(QStringLiteral("zoom-in"), dark_));
}

void DocumentBar::resizeEvent(QResizeEvent* event) {
  QWidget::resizeEvent(event);
  UpdateChildVisibility();
}

void DocumentBar::UpdateChildVisibility() {
  // 文档条现在横跨整窗，阈值按窗口宽度定；搜索框是主入口，只压缩不隐藏
  const int width = this->width();
  zoom_group_->setVisible(width >= 860);
  appearance_button_->setVisible(width >= 720);
  copy_button_->setVisible(width >= 620);
  find_edit_->setVisible(true);
  find_edit_->setFixedWidth(width >= 620 ? 240 : 160);
  UpdatePathElision();
}

void DocumentBar::UpdatePathElision() {
  if (path_label_ == nullptr) {
    return;
  }
  const int available = path_label_->width() > 0 ? path_label_->width() : 200;
  path_label_->setText(
      QFontMetrics(path_label_->font())
          .elidedText(display_path_, Qt::ElideMiddle, available));
}

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
