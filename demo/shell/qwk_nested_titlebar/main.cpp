// Minimal QWindowKit shell experiment: the left rail/sidebar reaches the
// window's top edge while the title bar only spans the right main column.
#include <QApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QMainWindow>
#include <QPushButton>
#include <QSplitter>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWKWidgets/widgetwindowagent.h>
#include <QWidget>

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

class ShellDemo final : public QMainWindow {
 public:
  ShellDemo() {
    setAttribute(Qt::WA_DontCreateNativeAncestors);
    setWindowTitle(QStringLiteral("QWindowKit shell demo"));
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
        QStringLiteral("drag the right title bar / click left rail"), status));
    root_layout->addWidget(status);
    setCentralWidget(root_);

    agent_ = new QWK::WidgetWindowAgent(this);
    agent_->setup(this);
    agent_->setTitleBar(title_bar_);
    for (QWidget* w : hit_test_widgets_) {
      agent_->setHitTestVisible(w, true);
    }
    agent_->setSystemButton(QWK::WindowAgentBase::Minimize, min_button_);
    agent_->setSystemButton(QWK::WindowAgentBase::Maximize, max_button_);
    agent_->setSystemButton(QWK::WindowAgentBase::Close, close_button_);

    setStyleSheet(QStringLiteral(R"(
      QWidget { color: #e6e6e6; background: #202124; font: 13px "Segoe UI"; }
      #demoRail { background: #1b1c1f; border-right: 1px solid #333438; }
      #demoSidebar { background: #252629; }
      #demoTitleBar { background: #2b2c30; }
      #demoToolbar { background: #252629; border-bottom: 1px solid #333438; }
      #demoContent { background: #1d1e20; color: #b6b8bd; font-size: 18px; }
      #demoStatus { background: #173a5e; color: #b8c5d6; }
      #demoSidebarHeader { background: #252629; border-bottom: 1px solid #333438; }
      QToolButton { border: 0; border-radius: 5px; padding: 4px 6px; }
      QToolButton:hover { background: #ffffff18; }
      #demoCollapse { background: #ffffff10; }
      #demoMenu { border-radius: 0; }
      #demoTheme, #demoMin, #demoMax, #demoClose { border-radius: 0; }
      #demoClose:hover { background: #c42b1c; }
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

    auto* body =
        new QLabel(QStringLiteral("Sidebar starts at the window top edge.\n\n"
                                  "Drag this area should not move the window.\n"
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
      status_->setText(hidden ? QStringLiteral("Sidebar collapsed; rail still "
                                               "owns the top-left edge")
                              : QStringLiteral("Sidebar expanded"));
    });
  }

  void BuildRightShell() {
    right_shell_ = new QWidget(splitter_);
    right_shell_->setObjectName(QStringLiteral("demoRightShell"));
    auto* layout = new QVBoxLayout(right_shell_);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    title_bar_ = new QWidget(right_shell_);
    title_bar_->setObjectName(QStringLiteral("demoTitleBar"));
    title_bar_->setFixedHeight(40);
    auto* title_layout = new QHBoxLayout(title_bar_);
    title_layout->setContentsMargins(2, 0, 0, 0);
    title_layout->setSpacing(2);

    menu_ = MakeButton(title_bar_, QStringLiteral("demoMenu"),
                       QStringLiteral("Menu"));
    menu_->setFixedSize(48, 40);
    connect(menu_, &QToolButton::clicked, this, [this] {
      log_->setText(QStringLiteral("application menu clicked"));
      status_->setText(QStringLiteral("Menu button: hit test works"));
    });
    title_layout->addWidget(menu_);

    auto* tab = new QLabel(QStringLiteral("  A document tab  "), title_bar_);
    tab->setObjectName(QStringLiteral("demoTab"));
    tab->setStyleSheet(
        QStringLiteral("background:#1d1e20; border-radius:8px 8px 0 0;"
                       "padding:7px 10px;"));
    title_layout->addWidget(tab);
    hit_test_widgets_.push_back(tab);
    title_layout->addStretch(1);

    theme_ = MakeButton(title_bar_, QStringLiteral("demoTheme"),
                        QStringLiteral("Sun"));
    theme_->setFixedSize(38, 40);
    connect(theme_, &QToolButton::clicked, this, [this] {
      log_->setText(QStringLiteral("theme button clicked"));
      status_->setText(QStringLiteral("Theme button: hit test works"));
    });
    title_layout->addWidget(theme_);

    min_button_ =
        MakeButton(title_bar_, QStringLiteral("demoMin"), QStringLiteral("_"));
    max_button_ =
        MakeButton(title_bar_, QStringLiteral("demoMax"), QStringLiteral("[]"));
    close_button_ = MakeButton(title_bar_, QStringLiteral("demoClose"),
                               QStringLiteral("X"));
    for (QToolButton* b : {min_button_, max_button_, close_button_}) {
      b->setFixedSize(46, 40);
      title_layout->addWidget(b);
    }
    connect(min_button_, &QToolButton::clicked, this, &QWidget::showMinimized);
    connect(max_button_, &QToolButton::clicked, this,
            [this] { isMaximized() ? showNormal() : showMaximized(); });
    connect(close_button_, &QToolButton::clicked, this, &QWidget::close);
    hit_test_widgets_ = {menu_, theme_, min_button_, max_button_,
                         close_button_};
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
        QStringLiteral("Main content\n\nThe title bar only covers this right "
                       "column.\nThe left shell owns the window's top-left "
                       "corner.\n\nDrag the title bar to move the window."),
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
  QWidget* title_bar_ = nullptr;
  QToolButton* collapse_ = nullptr;
  QToolButton* menu_ = nullptr;
  QToolButton* theme_ = nullptr;
  QToolButton* min_button_ = nullptr;
  QToolButton* max_button_ = nullptr;
  QToolButton* close_button_ = nullptr;
  QLabel* log_ = nullptr;
  QLabel* status_ = nullptr;
  QWK::WidgetWindowAgent* agent_ = nullptr;
  QList<QWidget*> hit_test_widgets_;
};

}  // namespace

int main(int argc, char** argv) {
  QApplication app(argc, argv);
  ShellDemo window;
  window.show();
  return app.exec();
}
