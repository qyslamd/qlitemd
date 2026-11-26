#include "activity_bar.h"

#include <QButtonGroup>
#include <QPaintEvent>
#include <QPainter>
#include <QSize>
#include <QToolButton>
#include <QVBoxLayout>
#include <QtDebug>

#include "theme_icons.h"

namespace {

// 42px 带里的按钮与图标尺寸；按钮居中后两侧各留 6px
constexpr int kButtonSize = 30;
constexpr int kIconSize = 20;

QToolButton* MakeActivityButton(QWidget* parent, const QString& name,
                                const QString& tooltip) {
  auto* button = new QToolButton(parent);
  button->setObjectName(name);
  button->setAutoRaise(true);
  button->setCheckable(true);
  button->setToolButtonStyle(Qt::ToolButtonIconOnly);
  // button->setFixedSize(kButtonSize, kButtonSize);
  button->setIconSize(QSize(kIconSize, kIconSize));
  button->setToolTip(tooltip);
  button->setAccessibleName(tooltip);
  return button;
}

}  // namespace

ActivityBar::ActivityBar(QWidget* parent) : QFrame(parent) {
  setObjectName(QStringLiteral("activityBar"));
  setFixedWidth(42);
  setFrameShape(QFrame::StyledPanel);
  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(3, 6, 3, 6);
  layout->setSpacing(4);

  // 顺序必须与 SideView 的数值一致：下标即持久化 id
  buttons_ = {
      MakeActivityButton(this, QStringLiteral("activityDocuments"),
                         tr("Documents (Ctrl+Shift+E)")),
      MakeActivityButton(this, QStringLiteral("activitySearch"),
                         tr("Search (Ctrl+Shift+F)")),
      MakeActivityButton(this, QStringLiteral("activityBookmarks"),
                         tr("Bookmarks (Ctrl+Shift+B)")),
      MakeActivityButton(this, QStringLiteral("activityRenderStatus"),
                         tr("Render Status (Ctrl+Shift+R)")),
  };
  group_ = new QButtonGroup(this);
  group_->setExclusive(true);
  for (int i = 0; i < buttons_.size(); ++i) {
    group_->addButton(buttons_[i], i);
    layout->addWidget(buttons_[i]);
    // 已选中的按钮点击也要上报，宿主用它在展开/收起侧栏之间切换
    const auto view = static_cast<SideView>(i);
    connect(buttons_[i], &QToolButton::clicked, this,
            [this, view] { emit viewActivated(view); });
  }
  layout->addStretch(1);
  // 底部只放全局入口：设置
  settings_button_ = new QToolButton(this);
  settings_button_->setObjectName(QStringLiteral("activitySettings"));
  settings_button_->setAutoRaise(true);
  settings_button_->setToolButtonStyle(Qt::ToolButtonIconOnly);
  settings_button_->setFixedSize(kButtonSize, kButtonSize);
  settings_button_->setIconSize(QSize(kIconSize, kIconSize));
  settings_button_->setToolTip(tr("Settings (Ctrl+,)"));
  settings_button_->setAccessibleName(tr("Settings"));
  layout->addWidget(settings_button_);
  connect(settings_button_, &QToolButton::clicked, this,
          &ActivityBar::settingsRequested);

  SetActiveView(SideView::kDocuments);
  SetThemeDark(false);
}

void ActivityBar::SetThemeDark(bool dark) {
  button(SideView::kDocuments)
      ->setIcon(ThemeIcon(QStringLiteral("folder"), dark));
  button(SideView::kSearch)->setIcon(ThemeIcon(QStringLiteral("search"), dark));
  button(SideView::kBookmarks)
      ->setIcon(ThemeIcon(QStringLiteral("star-outline"), dark));
  button(SideView::kRenderStatus)
      ->setIcon(ThemeIcon(QStringLiteral("render-status"), dark));
  settings_button_->setIcon(ThemeIcon(QStringLiteral("settings"), dark));
}

void ActivityBar::SetActiveView(SideView view) {
  QToolButton* target = button(view);
  if (target == nullptr) {
    return;
  }
  active_view_ = view;
  target->setChecked(true);
  update();
}

void ActivityBar::paintEvent(QPaintEvent* event) {
  QFrame::paintEvent(event);
  const QToolButton* active = button(active_view_);
  if (active == nullptr) {
    return;
  }
  const QRect button_rect = active->geometry();
  QPainter painter(this);
  painter.fillRect(QRect(0, button_rect.top() + 3, 4, button_rect.height() - 6),
                   palette().color(QPalette::Highlight));
}

QToolButton* ActivityBar::button(SideView view) const {
  const int index = static_cast<int>(view);
  if (index < 0 || index >= buttons_.size()) {
    return nullptr;
  }
  return buttons_[index];
}
