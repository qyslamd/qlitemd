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
    connect(buttons_[i], &QToolButton::clicked, this,
            [this, i] { emit viewActivated(i); });
  }
  layout->addStretch(1);
  SetActiveView(kDocuments);
  SetThemeDark(false);
}

void ActivityBar::SetThemeDark(bool dark) {
  buttons_[kDocuments]->setIcon(ThemeIcon(QStringLiteral("folder"), dark));
  buttons_[kSearch]->setIcon(ThemeIcon(QStringLiteral("search"), dark));
  buttons_[kBookmarks]->setIcon(
      ThemeIcon(QStringLiteral("star-outline"), dark));
  buttons_[kRenderStatus]->setIcon(
      ThemeIcon(QStringLiteral("render-status"), dark));
}

void ActivityBar::SetActiveView(int view) {
  if (view < 0 || view >= buttons_.size()) {
    return;
  }
  active_view_ = view;
  buttons_[view]->setChecked(true);
  update();
}

void ActivityBar::paintEvent(QPaintEvent* event) {
  QWidget::paintEvent(event);
  if (active_view_ < 0 || active_view_ >= buttons_.size()) {
    return;
  }
  const QRect button = buttons_[active_view_]->geometry();
  QPainter painter(this);
  painter.fillRect(QRect(0, button.top() + 3, 2, button.height() - 6),
                   palette().color(QPalette::Highlight));
}

QToolButton* ActivityBar::button(int view) const {
  if (view < 0 || view >= buttons_.size()) {
    return nullptr;
  }
  return buttons_[view];
}
