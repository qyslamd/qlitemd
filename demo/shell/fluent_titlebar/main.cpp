// Validate a real FluentTitleBar as a nested right-column title bar under
// QWindowKit, with the activity rail + sidebar owning the window top-left.
#include <fluentui3style.h>

#include <QApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QMouseEvent>
#include <QSplitter>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWKWidgets/widgetwindowagent.h>
#include <QWidget>
#include <QWindow>

#include "fluenttitlebar.h"

namespace {

QToolButton* MakeButton(QWidget* parent, const QString& name,
                        const QString& text) {
  auto* button = new QToolButton(parent);
  button->setObjectName(name);
  button->setText(text);
  button->setAutoRaise(true);
  button->setFocusPolicy(Qt::NoFocus);
  button->setFixedSize(34, 30);
  return button;
}

class WindowDragArea final : public QWidget {
 public:
  using QWidget::QWidget;

 protected:
  void mousePressEvent(QMouseEvent* event) override {
    if (event->button() == Qt::LeftButton && window() != nullptr) {
      if (window()->windowHandle() != nullptr) {
        window()->windowHandle()->startSystemMove();
        return;
      }
    }
    QWidget::mousePressEvent(event);
  }

  void mouseDoubleClickEvent(QMouseEvent* event) override {
    if (event->button() == Qt::LeftButton && window() != nullptr) {
      window()->isMaximized() ? window()->showNormal()
                              : window()->showMaximized();
      return;
    }
    QWidget::mouseDoubleClickEvent(event);
  }
};

class ShellDemo final : public QMainWindow {
 public:
  ShellDemo() {
    setAttribute(Qt::WA_DontCreateNativeAncestors);
    setWindowTitle(QStringLiteral("FluentTitleBar shell demo"));
    resize(1080, 720);

    root_ = new QWidget(this);
    root_->setObjectName(QStringLiteral("demoRoot"));
    auto* root_layout = new QVBoxLayout(root_);
    root_layout->setContentsMargins(0, 0, 0, 0);
    root_layout->setSpacing(0);

    splitter_ = new QSplitter(Qt::Horizontal, root_);
    splitter_->setObjectName(QStringLiteral("shellSplitter"));
    splitter_->setChildrenCollapsible(false);
    splitter_->setHandleWidth(4);

    BuildLeftShell();
    BuildRightShell();
    splitter_->addWidget(left_shell_);
    splitter_->addWidget(right_shell_);
    splitter_->setStretchFactor(0, 0);
    splitter_->setStretchFactor(1, 1);
    splitter_->setSizes({300, 780});
    root_layout->addWidget(splitter_, 1);

    auto* status = new QWidget(root_);
    status->setObjectName(QStringLiteral("demoStatus"));
    status->setFixedHeight(26);
    auto* status_layout = new QHBoxLayout(status);
    status_layout->setContentsMargins(10, 0, 10, 0);
    status_ = new QLabel(QStringLiteral("Ready"), status);
    status_layout->addWidget(status_);
    status_layout->addStretch(1);
    status_layout->addWidget(new QLabel(
        QStringLiteral("real FluentTitleBar / QWindowKit agent"), status));
    root_layout->addWidget(status);
    setCentralWidget(root_);

    agent_ = new QWK::WidgetWindowAgent(this);
    agent_->setup(this);
    agent_->setTitleBar(title_bar_);
    for (QWidget* w : hit_test_widgets_) {
      agent_->setHitTestVisible(w, true);
    }
    agent_->setSystemButton(QWK::WindowAgentBase::Minimize,
                            title_bar_->minButton());
    agent_->setSystemButton(QWK::WindowAgentBase::Maximize,
                            title_bar_->maxButton());
    agent_->setSystemButton(QWK::WindowAgentBase::Close,
                            title_bar_->closeButton());

    setStyleSheet(QStringLiteral(R"(
      #demoRail { background: #1b1c1f; border-right: 1px solid #333438; }
      #demoSidebar { background: #252629; }
      #demoToolbar { background: #252629; border-bottom: 1px solid #333438; }
      #demoContent { background: #1d1e20; color: #b6b8bd; font-size: 18px; }
      #demoStatus { background: #173a5e; color: #b8c5d6; }
      #demoSidebarHeader { background: #252629; border-bottom: 1px solid #333438; }
    )"));
  }

 private:
  void BuildLeftShell() {
    left_shell_ = new QWidget(splitter_);
    left_shell_->setObjectName(QStringLiteral("demoLeftShell"));
    auto* layout = new QHBoxLayout(left_shell_);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto* rail = new QWidget(left_shell_);
    rail->setObjectName(QStringLiteral("demoRail"));
    rail->setFixedWidth(42);
    auto* rail_layout = new QVBoxLayout(rail);
    rail_layout->setContentsMargins(0, 0, 0, 0);
    rail_layout->setSpacing(2);
    collapse_ =
        MakeButton(rail, QStringLiteral("demoCollapse"), QStringLiteral("<"));
    collapse_->setFixedSize(42, 40);
    collapse_->setToolTip(QStringLiteral("Toggle sidebar without dragging"));
    rail_layout->addWidget(collapse_);
    for (const QString& label : {QStringLiteral("F"), QStringLiteral("S"),
                                 QStringLiteral("B"), QStringLiteral("R")}) {
      auto* b = MakeButton(rail, QString(), label);
      b->setFixedSize(38, 38);
      b->setToolTip(label);
      rail_layout->addWidget(b, 0, Qt::AlignHCenter);
    }
    rail_layout->addStretch(1);
    rail_layout->addWidget(MakeButton(rail, QString(), QStringLiteral("G")), 0,
                           Qt::AlignHCenter);
    layout->addWidget(rail);

    sidebar_ = new QWidget(left_shell_);
    sidebar_->setObjectName(QStringLiteral("demoSidebar"));
    auto* side_layout = new QVBoxLayout(sidebar_);
    side_layout->setContentsMargins(0, 0, 0, 0);
    side_layout->setSpacing(0);

    auto* header = new QWidget(sidebar_);
    header->setObjectName(QStringLiteral("demoSidebarHeader"));
    header->setFixedHeight(40);
    auto* header_layout = new QHBoxLayout(header);
    header_layout->setContentsMargins(10, 0, 6, 0);
    auto* title = new QLabel(QStringLiteral("Documents"), header);
    title->setStyleSheet(QStringLiteral("font-weight: 600;"));
    header_layout->addWidget(title);
    header_layout->addStretch(1);
    auto* open = MakeButton(header, QString(), QStringLiteral("O"));
    open->setToolTip(QStringLiteral("Open folder"));
    header_layout->addWidget(open);
    auto* refresh = MakeButton(header, QString(), QStringLiteral("R"));
    refresh->setToolTip(QStringLiteral("Refresh workspace"));
    header_layout->addWidget(refresh);
    side_layout->addWidget(header);

    auto* body = new QLabel(
        QStringLiteral("FluentTitleBar is nested in the right column.\n\n"
                       "Left rail/sidebar owns the top-left edge.\n"
                       "Drag the leading blank area to move.\n"
                       "Click < on the rail to collapse."),
        sidebar_);
    body->setWordWrap(true);
    body->setContentsMargins(14, 16, 14, 16);
    body->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    side_layout->addWidget(body, 1);

    log_ = new QLabel(QStringLiteral("interaction log"), sidebar_);
    log_->setStyleSheet(QStringLiteral("color:#8f9298; padding:8px;"));
    side_layout->addWidget(log_);
    layout->addWidget(sidebar_, 1);

    connect(collapse_, &QToolButton::clicked, this, [this] {
      const bool hidden = sidebar_->isVisible();
      sidebar_->setVisible(!hidden);
      left_shell_->setMinimumWidth(hidden ? 42 : 0);
      left_shell_->setMaximumWidth(hidden ? 42 : QWIDGETSIZE_MAX);
      splitter_->setSizes({hidden ? 42 : 300, width() - (hidden ? 42 : 300)});
      log_->setText(hidden ? QStringLiteral("sidebar collapsed by rail button")
                           : QStringLiteral("sidebar expanded by rail button"));
      status_->setText(hidden ? QStringLiteral("Sidebar collapsed; rail keeps "
                                               "the top-left edge")
                              : QStringLiteral("Sidebar expanded"));
    });
  }

  void BuildRightShell() {
    right_shell_ = new QWidget(splitter_);
    right_shell_->setObjectName(QStringLiteral("demoRightShell"));
    auto* layout = new QVBoxLayout(right_shell_);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    title_bar_ = new FluentTitleBar(this);
    title_bar_->setTitleVisible(false);
    title_bar_->searchLineEdit()->hide();

    auto* leading = new QWidget(title_bar_);
    leading->setObjectName(QStringLiteral("demoTitleLeading"));
    leading->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    auto* leading_layout = new QHBoxLayout(leading);
    leading_layout->setContentsMargins(0, 4, 0, 0);
    leading_layout->setSpacing(2);

    menu_ =
        MakeButton(leading, QStringLiteral("demoMenu"), QStringLiteral("Menu"));
    menu_->setFixedSize(48, 32);
    connect(menu_, &QToolButton::clicked, this, [this] {
      log_->setText(QStringLiteral("FluentTitleBar menu clicked"));
      status_->setText(QStringLiteral("FluentTitleBar menu: hit test works"));
    });
    leading_layout->addWidget(menu_);

    auto* tab = new QLabel(QStringLiteral("  A document tab  "), leading);
    tab->setObjectName(QStringLiteral("demoTab"));
    tab->setStyleSheet(
        QStringLiteral("background:#1d1e20; border-radius:8px 8px 0 0;"
                       "padding:7px 10px;"));
    tab->setFixedHeight(32);
    leading_layout->addWidget(tab);
    auto* drag = new WindowDragArea(leading);
    drag->setObjectName(QStringLiteral("demoTitleDrag"));
    // Tabs may consume the whole leading area; always leave a visible drag
    // handle so a full tab strip cannot make the window unmovable.
    drag->setMinimumWidth(96);
    leading_layout->addWidget(drag, 1);
    title_bar_->setLeadingWidget(leading);
    if (auto* title_layout = qobject_cast<QHBoxLayout*>(title_bar_->layout())) {
      title_layout->setStretchFactor(leading, 1);
    }

    connect(title_bar_->themeButton(), &QToolButton::clicked, this, [this] {
      dark_ = !dark_;
      title_bar_->setThemeDark(dark_);
      log_->setText(QStringLiteral("FluentTitleBar theme clicked"));
      status_->setText(QStringLiteral("FluentTitleBar theme: hit test works"));
    });
    connect(title_bar_->pinButton(), &QToolButton::toggled, this,
            [this](bool pinned) {
              setWindowFlag(Qt::WindowStaysOnTopHint, pinned);
              show();
            });

    hit_test_widgets_ = {leading,
                         menu_,
                         tab,
                         title_bar_->themeButton(),
                         title_bar_->pinButton(),
                         title_bar_->minButton(),
                         title_bar_->maxButton(),
                         title_bar_->closeButton()};
    layout->addWidget(title_bar_);

    auto* toolbar = new QWidget(right_shell_);
    toolbar->setObjectName(QStringLiteral("demoToolbar"));
    toolbar->setFixedHeight(40);
    auto* toolbar_layout = new QHBoxLayout(toolbar);
    toolbar_layout->setContentsMargins(10, 0, 10, 0);
    toolbar_layout->addWidget(new QLabel(
        QStringLiteral("Back   Forward   Path / document.md"), toolbar));
    toolbar_layout->addStretch(1);
    toolbar_layout->addWidget(
        new QLabel(QStringLiteral("Find  1 / 2  Up  Down"), toolbar));
    layout->addWidget(toolbar);

    auto* content = new QLabel(
        QStringLiteral("Real FluentTitleBar\n\n"
                       "The title bar only covers the right column.\n"
                       "The left shell owns the top-left edge.\n\n"
                       "Drag the blank area after the tab to move.\n"
                       "Double click to maximize / restore."),
        right_shell_);
    content->setObjectName(QStringLiteral("demoContent"));
    content->setAlignment(Qt::AlignCenter);
    layout->addWidget(content, 1);
  }

  QWidget* root_ = nullptr;
  QSplitter* splitter_ = nullptr;
  QWidget* left_shell_ = nullptr;
  QWidget* sidebar_ = nullptr;
  QWidget* right_shell_ = nullptr;
  FluentTitleBar* title_bar_ = nullptr;
  QToolButton* collapse_ = nullptr;
  QToolButton* menu_ = nullptr;
  QLabel* log_ = nullptr;
  QLabel* status_ = nullptr;
  QWK::WidgetWindowAgent* agent_ = nullptr;
  QList<QWidget*> hit_test_widgets_;
  bool dark_ = true;
};

}  // namespace

int main(int argc, char** argv) {
  QApplication app(argc, argv);
  app.setProperty("_q_themestyle", 0);
  app.setProperty("_q_colorscheme", 1);
  app.setProperty("_q_widget_mode", 0);
  app.setStyle(new FluentUI3Style);
  app.setPalette(app.style()->standardPalette());
  ShellDemo window;
  window.show();
  return app.exec();
}
