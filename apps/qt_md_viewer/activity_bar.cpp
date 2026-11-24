#include "activity_bar.h"

#include <QButtonGroup>
#include <QPaintEvent>
#include <QPainter>
#include <QToolButton>
#include <QVBoxLayout>

#include "theme_icons.h"

namespace {

QToolButton* MakeActivityButton(QWidget* parent, const QString& name,
                                const QString& tooltip) {
  auto* button = new QToolButton(parent);
  button->setObjectName(name);
  button->setAutoRaise(true);
  button->setCheckable(true);
  button->setToolButtonStyle(Qt::ToolButtonIconOnly);
  button->setFixedSize(24, 24);
  button->setToolTip(tooltip);
  button->setAccessibleName(tooltip);
  return button;
}

}  // namespace

ActivityBar::ActivityBar(QWidget* parent) : QWidget(parent) {
  setObjectName(QStringLiteral("activityBar"));
  setFixedWidth(30);
  setAutoFillBackground(true);
  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(3, 6, 3, 6);
  layout->setSpacing(4);

  // 顺序必须与 SideView 的数值一致：下标即持久化 id
  buttons_ = {
      MakeActivityButton(this, QStringLiteral("activityDocuments"),
                         QStringLiteral("文档（Ctrl+Shift+E）")),
      MakeActivityButton(this, QStringLiteral("activitySearch"),
                         QStringLiteral("搜索（Ctrl+Shift+F）")),
      MakeActivityButton(this, QStringLiteral("activityBookmarks"),
                         QStringLiteral("收藏（Ctrl+Shift+B）")),
      MakeActivityButton(this, QStringLiteral("activityRenderStatus"),
                         QStringLiteral("渲染状态（Ctrl+Shift+R）")),
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
  settings_button_->setFixedSize(24, 24);
  settings_button_->setToolTip(QStringLiteral("设置（Ctrl+,）"));
  settings_button_->setAccessibleName(QStringLiteral("设置"));
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
  QWidget::paintEvent(event);
  const QToolButton* active = button(active_view_);
  if (active == nullptr) {
    return;
  }
  const QRect button_rect = active->geometry();
  QPainter painter(this);
  painter.fillRect(QRect(0, button_rect.top() + 3, 2, button_rect.height() - 6),
                   palette().color(QPalette::Highlight));
}

QToolButton* ActivityBar::button(SideView view) const {
  const int index = static_cast<int>(view);
  if (index < 0 || index >= buttons_.size()) {
    return nullptr;
  }
  return buttons_[index];
}
