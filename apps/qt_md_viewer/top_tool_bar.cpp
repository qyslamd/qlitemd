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
#include <QMenu>
#include <QPaintEvent>
#include <QPainter>
#include <QPalette>
#include <QResizeEvent>
#include <QSizePolicy>
#include <QToolButton>

#include "document_tab_bar.h"
#include "theme_icons.h"

namespace {

constexpr int kToolbarHeight = 40;
constexpr int kToolbarButtonSize = 28;
constexpr int kToolbarIconSize = 18;

QToolButton* MakeIconButton(QWidget* parent, const QString& name,
                            const QString& tooltip) {
  auto* button = new QToolButton(parent);
  button->setObjectName(name);
  button->setAutoRaise(true);
  button->setToolTip(tooltip);
  button->setToolButtonStyle(Qt::ToolButtonIconOnly);
  button->setFixedSize(kToolbarButtonSize, kToolbarButtonSize);
  button->setIconSize(QSize(kToolbarIconSize, kToolbarIconSize));
  return button;
}

// 半透明线条跟着 WindowText 走：浅色主题是黑线，深色主题是白线
QColor DimLineColor(const QPalette& palette, int alpha) {
  QColor color = palette.color(QPalette::WindowText);
  color.setAlpha(alpha);
  return color;
}

class ToolbarSeparator : public QWidget {
 public:
  explicit ToolbarSeparator(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("toolbarSeparator"));
    setFixedSize(1, 16);
  }

 protected:
  void paintEvent(QPaintEvent*) override {
    QPainter painter(this);
    painter.fillRect(rect(), DimLineColor(palette(), 30));
  }
};

// 给缩放这类成组控件一个圆角底，避免一排按钮全是同一层级
class ToolbarPill : public QWidget {
 public:
  explicit ToolbarPill(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("toolbarPill"));
  }

 protected:
  void paintEvent(QPaintEvent*) override {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QRectF pill = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    painter.setPen(QPen(DimLineColor(palette(), 24), 1.0));
    painter.setBrush(palette().color(QPalette::Base));
    painter.drawRoundedRect(pill, 6.0, 6.0);
  }
};

QWidget* MakeToolbarSeparator(QWidget* parent) {
  return new ToolbarSeparator(parent);
}

}  // namespace

TopToolBar::TopToolBar(QWidget* parent) : QToolBar(parent) {
  setObjectName(QStringLiteral("topToolBar"));
  setAllowedAreas(Qt::TopToolBarArea);
  setContextMenuPolicy(Qt::PreventContextMenu);
  ResetInvariants();
  setContentsMargins(0, 0, 0, 0);
  if (layout() != nullptr) {
    layout()->setContentsMargins(0, 0, 0, 0);
  }
}

void TopToolBar::ResetInvariants() {
  setMovable(false);
  setFloatable(false);
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  show();
}

void TopToolBar::paintEvent(QPaintEvent* event) {
  // 先让 QToolBar 走完基样式，再盖一层自己的 surface：用选中标签的
  // 底色，让标签和工具栏连成一片（Chrome/Edge 的做法）
  QToolBar::paintEvent(event);
  QPainter painter(this);
  const bool dark = palette().color(QPalette::Window).lightness() < 128;
  painter.fillRect(rect(), SelectedTabColorFor(dark));
  painter.fillRect(QRect(0, height() - 1, width(), 1),
                   DimLineColor(palette(), 32));
}

DocumentBar::DocumentBar(QWidget* parent) : QWidget(parent) {
  setObjectName(QStringLiteral("documentBar"));
  setFixedHeight(kToolbarHeight);
  auto* layout = new QHBoxLayout(this);
  layout->setContentsMargins(12, 0, 12, 0);
  layout->setSpacing(6);

  back_button_ =
      MakeIconButton(this, QStringLiteral("docBack"), tr("Back (Ctrl+Alt+←)"));
  forward_button_ = MakeIconButton(this, QStringLiteral("docForward"),
                                   tr("Forward (Ctrl+Alt+→)"));
  star_button_ = MakeIconButton(this, QStringLiteral("docStar"),
                                tr("Bookmark Current Document (Ctrl+D)"));
  star_button_->setCheckable(true);
  layout->addWidget(back_button_);
  layout->addWidget(forward_button_);
  layout->addWidget(star_button_);
  layout->addWidget(MakeToolbarSeparator(this), 0, Qt::AlignVCenter);

  path_label_ = new QLabel(this);
  path_label_->setObjectName(QStringLiteral("docPathLabel"));
  // 路径是上下文信息，低于文档动作一级
  path_label_->setForegroundRole(QPalette::PlaceholderText);
  path_label_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
  path_label_->setTextInteractionFlags(Qt::NoTextInteraction);
  layout->addWidget(path_label_, 1);

  copy_button_ = new QToolButton(this);
  copy_button_->setObjectName(QStringLiteral("docCopyPath"));
  copy_button_->setAutoRaise(true);
  copy_button_->setText(tr("Copy"));
  copy_button_->setToolButtonStyle(Qt::ToolButtonTextOnly);
  copy_button_->setToolTip(tr("Copy current document path"));
  layout->addWidget(copy_button_);

  find_edit_ = new QLineEdit(this);
  find_edit_->setObjectName(QStringLiteral("docFindEdit"));
  find_edit_->setPlaceholderText(tr("Find in Current Document (Ctrl+F)"));
  find_edit_->setClearButtonEnabled(true);
  find_edit_->setFixedWidth(240);
  layout->addWidget(find_edit_);
  layout->addWidget(MakeToolbarSeparator(this), 0, Qt::AlignVCenter);

  outline_button_ = MakeIconButton(this, QStringLiteral("docOutlineToggle"),
                                   tr("Outline (Ctrl+Shift+O)"));
  outline_button_->setCheckable(true);
  layout->addWidget(outline_button_);

  appearance_button_ = MakeIconButton(
      this, QStringLiteral("docAppearance"),
      tr("Reading appearance (font size, line height, measure)"));
  layout->addWidget(appearance_button_);

  zoom_group_ = new ToolbarPill(this);
  zoom_group_->setObjectName(QStringLiteral("docZoomGroup"));
  auto* zoom_layout = new QHBoxLayout(zoom_group_);
  zoom_layout->setContentsMargins(2, 2, 2, 2);
  zoom_layout->setSpacing(2);
  zoom_out_button_ = MakeIconButton(zoom_group_, QStringLiteral("docZoomOut"),
                                    tr("Zoom Out (Ctrl+-)"));
  zoom_reset_button_ = new QToolButton(zoom_group_);
  zoom_reset_button_->setObjectName(QStringLiteral("docZoomReset"));
  zoom_reset_button_->setAutoRaise(true);
  zoom_reset_button_->setText(QStringLiteral("100%"));
  zoom_reset_button_->setToolButtonStyle(Qt::ToolButtonTextOnly);
  zoom_reset_button_->setToolTip(tr("Reset Zoom (Ctrl+0)"));
  zoom_reset_button_->setMinimumWidth(46);
  zoom_in_button_ = MakeIconButton(zoom_group_, QStringLiteral("docZoomIn"),
                                   tr("Zoom In (Ctrl+=)"));
  zoom_layout->addWidget(zoom_out_button_);
  zoom_layout->addWidget(zoom_reset_button_);
  zoom_layout->addWidget(zoom_in_button_);
  layout->addWidget(zoom_group_, 0, Qt::AlignVCenter);

  // 导出放最右、最先让位：低频动作，窄窗下交给 File 菜单与命令面板兜底
  export_separator_ = MakeToolbarSeparator(this);
  layout->addWidget(export_separator_, 0, Qt::AlignVCenter);
  export_button_ = new QToolButton(this);
  export_button_->setObjectName(QStringLiteral("docExport"));
  export_button_->setAutoRaise(true);
  export_button_->setToolButtonStyle(Qt::ToolButtonIconOnly);
  export_button_->setFixedSize(kToolbarButtonSize, kToolbarButtonSize);
  export_button_->setIconSize(QSize(kToolbarIconSize, kToolbarIconSize));
  export_button_->setPopupMode(QToolButton::InstantPopup);
  export_button_->setToolTip(tr("Export… (HTML, PDF)"));
  export_menu_ = new QMenu(export_button_);
  export_menu_->setObjectName(QStringLiteral("docExportMenu"));
  export_button_->setMenu(export_menu_);
  layout->addWidget(export_button_);

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
  outline_button_->setIcon(ThemeIcon(QStringLiteral("outline"), dark_));
  appearance_button_->setIcon(ThemeIcon(QStringLiteral("appearance"), dark_));
  zoom_out_button_->setIcon(ThemeIcon(QStringLiteral("zoom-out"), dark_));
  zoom_in_button_->setIcon(ThemeIcon(QStringLiteral("zoom-in"), dark_));
  export_button_->setIcon(ThemeIcon(QStringLiteral("export"), dark_));
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
  // 导出频率最低，最先隐藏；入口仍在 File 菜单与命令面板
  export_button_->setVisible(width >= 900);
  export_separator_->setVisible(width >= 900);
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
  setFixedHeight(kToolbarHeight);
  auto* layout = new QHBoxLayout(this);
  layout->setContentsMargins(12, 0, 12, 0);
  layout->setSpacing(6);

  auto* mark = new QLabel(this);
  mark->setObjectName(QStringLiteral("welcomeMark"));
  mark->setPixmap(
      QIcon(QStringLiteral(":/resources/svg/icon.svg")).pixmap(16, 16));
  mark->setFixedSize(16, 16);
  layout->addWidget(mark);

  auto* title = new QLabel(tr("Welcome"), this);
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
                                      tr("Quick open (Ctrl+P)"));
  layout->addWidget(quick_open_button_);

  connect(quick_open_button_, &QToolButton::clicked, this,
          &WelcomeBar::quickOpenRequested);

  SetThemeDark(false);
  SetWorkspaceName(QString());
}

void WelcomeBar::SetWorkspaceName(const QString& root) {
  if (root.isEmpty()) {
    workspace_display_ = tr("No workspace open");
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
