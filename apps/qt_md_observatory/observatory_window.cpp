#include "observatory_window.h"

#include <QApplication>
#include <QCloseEvent>
#include <QDir>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QShortcut>
#include <QShowEvent>
#include <QSplitter>
#include <QTimer>
#include <QVBoxLayout>
#include <QWKWidgets/widgetwindowagent.h>
#include <QWindow>

#include "app_theme.h"
#include "command_palette.h"
#include "document_rail.h"
#include "instrument_math.h"
#include "markdown_view.h"
#include "title_bar.h"
#include "workspace_model.h"
#include "workspace_panel.h"

ObservatoryWindow::ObservatoryWindow(QWidget* parent) : QWidget(parent) {
  BuildUi();
  model_ = new WorkspaceModel(this);
  workspace_panel_->SetModel(model_);

  QSettings settings;
  dark_ = settings.value(QStringLiteral("appearance/dark"), true).toBool();
  ApplyTheme(dark_);
}

void ObservatoryWindow::BuildUi() {
  setObjectName(QStringLiteral("observatoryRoot"));
  setWindowTitle(tr("qt_md_observatory"));
  setMinimumSize(1024, 680);
  resize(1440, 900);

  auto* root = new QVBoxLayout(this);
  root->setContentsMargins(0, 0, 0, 0);
  root->setSpacing(0);

  title_bar_ = new TitleBar(this);
  root->addWidget(title_bar_);

  splitter_ = new QSplitter(Qt::Horizontal, this);
  splitter_->setObjectName(QStringLiteral("mainSplitter"));
  splitter_->setChildrenCollapsible(false);
  splitter_->setHandleWidth(1);

  workspace_panel_ = new WorkspacePanel(splitter_);
  workspace_panel_->setMinimumWidth(230);
  workspace_panel_->setMaximumWidth(380);
  connect(workspace_panel_, &WorkspacePanel::documentActivated, this,
          [this](int index) { OpenDocument(index); });

  auto* center = new QWidget(splitter_);
  center->setObjectName(QStringLiteral("documentColumn"));
  auto* center_layout = new QVBoxLayout(center);
  center_layout->setContentsMargins(0, 0, 0, 0);
  center_layout->setSpacing(0);

  auto* header = new QWidget(center);
  header->setFixedHeight(34);
  auto* header_layout = new QHBoxLayout(header);
  header_layout->setContentsMargins(18, 0, 18, 0);
  breadcrumb_ = new QLabel(header);
  breadcrumb_->setObjectName(QStringLiteral("breadcrumb"));
  breadcrumb_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
  breadcrumb_->setMinimumWidth(0);
  header_layout->addWidget(breadcrumb_, 1);
  auto* palette_button = new QPushButton(tr("Search"), header);
  palette_button->setObjectName(QStringLiteral("paletteButton"));
  palette_button->setAccessibleName(tr("Command palette"));
  palette_button->setToolTip(tr("Command palette (Ctrl+K)"));
  palette_button->setFixedHeight(24);
  header_layout->addWidget(palette_button);
  connect(palette_button, &QPushButton::clicked, this,
          &ObservatoryWindow::OpenPalette);

  auto* previous_button = new QPushButton(QStringLiteral("<"), header);
  previous_button->setObjectName(QStringLiteral("previousDocument"));
  previous_button->setAccessibleName(tr("Previous document"));
  previous_button->setToolTip(tr("Previous document (Alt+Up)"));
  previous_button->setFixedSize(28, 24);
  auto* next_button = new QPushButton(QStringLiteral(">"), header);
  next_button->setObjectName(QStringLiteral("nextDocument"));
  next_button->setAccessibleName(tr("Next document"));
  next_button->setToolTip(tr("Next document (Alt+Down)"));
  next_button->setFixedSize(28, 24);
  header_layout->addWidget(previous_button);
  header_layout->addWidget(next_button);
  center_layout->addWidget(header);
  connect(previous_button, &QPushButton::clicked, this,
          [this] { OpenRelative(-1); });
  connect(next_button, &QPushButton::clicked, this,
          [this] { OpenRelative(1); });

  resume_bar_ = new QWidget(center);
  resume_bar_->setObjectName(QStringLiteral("resumeBar"));
  resume_bar_->setFixedHeight(38);
  auto* resume_layout = new QHBoxLayout(resume_bar_);
  resume_layout->setContentsMargins(18, 0, 12, 0);
  resume_layout->setSpacing(10);
  resume_text_ = new QLabel(resume_bar_);
  resume_text_->setObjectName(QStringLiteral("resumeText"));
  resume_top_ = new QPushButton(tr("Start from top"), resume_bar_);
  resume_top_->setObjectName(QStringLiteral("resumeTop"));
  resume_close_ = new QPushButton(tr("Dismiss"), resume_bar_);
  resume_close_->setObjectName(QStringLiteral("resumeClose"));
  resume_layout->addWidget(resume_text_);
  resume_layout->addStretch(1);
  resume_layout->addWidget(resume_top_);
  resume_layout->addWidget(resume_close_);
  resume_bar_->hide();
  center_layout->addWidget(resume_bar_);

  view_ = new MarkdownView(center);
  view_->setObjectName(QStringLiteral("markdownView"));
  center_layout->addWidget(view_, 1);

  document_rail_ = new DocumentRail(splitter_);
  document_rail_->setMinimumWidth(250);
  document_rail_->setMaximumWidth(360);
  connect(document_rail_, &DocumentRail::anchorActivated, this,
          [this](const QString& anchor) { view_->ScrollToAnchor(anchor); });

  splitter_->addWidget(workspace_panel_);
  splitter_->addWidget(center);
  splitter_->addWidget(document_rail_);
  splitter_->setStretchFactor(0, 0);
  splitter_->setStretchFactor(1, 1);
  splitter_->setStretchFactor(2, 0);
  splitter_->setSizes({290, 860, 290});
  root->addWidget(splitter_, 1);

  palette_ = new CommandPalette(this);
  connect(palette_, &CommandPalette::closed, this,
          [this] { view_->setFocus(Qt::OtherFocusReason); });

  window_agent_ = new QWK::WidgetWindowAgent(this);
  window_agent_->setup(this);
  window_agent_->setTitleBar(title_bar_);
  window_agent_->setSystemButton(QWK::WindowAgentBase::Minimize,
                                 title_bar_->min_button());
  window_agent_->setSystemButton(QWK::WindowAgentBase::Maximize,
                                 title_bar_->max_button());
  window_agent_->setSystemButton(QWK::WindowAgentBase::Close,
                                 title_bar_->close_button());
  window_agent_->setHitTestVisible(title_bar_->theme_button(), true);
  window_agent_->setHitTestVisible(title_bar_->min_button(), true);
  window_agent_->setHitTestVisible(title_bar_->max_button(), true);
  window_agent_->setHitTestVisible(title_bar_->close_button(), true);

  connect(title_bar_, &TitleBar::themeToggled, this,
          [this] { ApplyTheme(!dark_); });
  connect(view_, &MarkdownView::viewportScrolled, this,
          &ObservatoryWindow::UpdateProgressFromScroll);
  connect(view_, &MarkdownView::renderFinished, this,
          [this](quint64) { OnRenderFinished(); });
  connect(resume_top_, &QPushButton::clicked, this, [this] {
    view_->SetScrollPosition(0);
    UpdateProgressFromScroll(0);
    ShowResumeBar(false, 0);
  });
  connect(resume_close_, &QPushButton::clicked, this,
          [this] { ShowResumeBar(false, 0); });

  auto* next_document =
      new QShortcut(QKeySequence(QStringLiteral("Alt+Down")), this);
  connect(next_document, &QShortcut::activated, this,
          [this] { OpenRelative(1); });
  auto* previous_document =
      new QShortcut(QKeySequence(QStringLiteral("Alt+Up")), this);
  connect(previous_document, &QShortcut::activated, this,
          [this] { OpenRelative(-1); });

  auto* palette_shortcut =
      new QShortcut(QKeySequence(QStringLiteral("Ctrl+K")), this);
  connect(palette_shortcut, &QShortcut::activated, this,
          &ObservatoryWindow::OpenPalette);

  auto add_key = [this](const QKeySequence& keys,
                        void (ObservatoryWindow::*method)(int), int argument) {
    auto* shortcut = new QShortcut(keys, this);
    connect(shortcut, &QShortcut::activated, this,
            [this, method, argument] { (this->*method)(argument); });
  };
  add_key(QKeySequence(QStringLiteral("[")), &ObservatoryWindow::JumpSection,
          -1);
  add_key(QKeySequence(QStringLiteral("]")), &ObservatoryWindow::JumpSection,
          1);
  add_key(QKeySequence(QStringLiteral("j")), &ObservatoryWindow::ScrollBy, 80);
  add_key(QKeySequence(QStringLiteral("k")), &ObservatoryWindow::ScrollBy, -80);

  auto* workspace_shortcut =
      new QShortcut(QKeySequence(QStringLiteral("W")), this);
  connect(workspace_shortcut, &QShortcut::activated, this, [this] {
    if (!IsTyping()) {
      ToggleWorkspace();
    }
  });
  auto* rail_shortcut = new QShortcut(QKeySequence(QStringLiteral("R")), this);
  connect(rail_shortcut, &QShortcut::activated, this, [this] {
    if (!IsTyping()) {
      ToggleRail();
    }
  });
  auto* focus_shortcut = new QShortcut(QKeySequence(QStringLiteral("F")), this);
  connect(focus_shortcut, &QShortcut::activated, this, [this] {
    if (!IsTyping()) {
      SetFocusMode(!focus_mode_);
    }
  });
  auto* escape_shortcut = new QShortcut(QKeySequence(Qt::Key_Escape), this);
  connect(escape_shortcut, &QShortcut::activated, this,
          &ObservatoryWindow::HandleEscape);
}

void ObservatoryWindow::OpenPalette() {
  if (palette_->is_open()) {
    palette_->Close();
    return;
  }
  QVector<PaletteItem> items;
  for (int i = 0; i < model_->count(); ++i) {
    const WorkspaceDocument& document = model_->documents()[i];
    PaletteItem item;
    item.category = tr("DOCUMENT");
    item.title = document.title;
    item.detail = document.relative_path;
    item.action = [this, i] { OpenDocument(i); };
    items.push_back(std::move(item));
  }
  if (current_index_ >= 0 && current_index_ < model_->count()) {
    for (const MarkdownHeading& heading : view_->outline()) {
      PaletteItem item;
      item.category = tr("SECTION");
      item.title = heading.text;
      item.detail = QStringLiteral("H%1").arg(heading.level);
      const QString anchor = heading.anchor;
      item.action = [this, anchor] { view_->ScrollToAnchor(anchor); };
      items.push_back(std::move(item));
    }
  }

  auto add_command = [this, &items](const QString& title, const QString& detail,
                                    std::function<void()> action) {
    PaletteItem item;
    item.category = tr("COMMAND");
    item.title = title;
    item.detail = detail;
    item.action = std::move(action);
    items.push_back(std::move(item));
  };
  add_command(tr("Toggle theme"), tr("Light / dark"),
              [this] { ApplyTheme(!dark_); });
  add_command(tr("Toggle workspace panel"), tr("W"),
              [this] { ToggleWorkspace(); });
  add_command(tr("Toggle document rail"), tr("R"), [this] { ToggleRail(); });
  add_command(tr("Toggle section spine"), tr("Collapse / expand"),
              [this] { document_rail_->ToggleSpine(); });
  add_command(tr("Focus mode"), tr("F"),
              [this] { SetFocusMode(!focus_mode_); });
  add_command(tr("Next document"), tr("Alt+Down"), [this] { OpenRelative(1); });
  add_command(tr("Previous document"), tr("Alt+Up"),
              [this] { OpenRelative(-1); });
  add_command(tr("Start from top"), tr("Scroll to the beginning"), [this] {
    view_->SetScrollPosition(0);
    UpdateProgressFromScroll(0);
    ShowResumeBar(false, 0);
  });

  palette_->SetItems(std::move(items));
  palette_->Open();
}

void ObservatoryWindow::ToggleWorkspace() {
  workspace_panel_->setVisible(!workspace_panel_->isVisible());
}

void ObservatoryWindow::ToggleRail() {
  document_rail_->setVisible(!document_rail_->isVisible());
}

void ObservatoryWindow::SetFocusMode(bool enabled) {
  if (focus_mode_ == enabled) {
    return;
  }
  focus_mode_ = enabled;
  if (enabled) {
    workspace_before_focus_ = workspace_panel_->isVisible();
    rail_before_focus_ = document_rail_->isVisible();
    sizes_before_focus_ = splitter_->sizes();
    workspace_panel_->hide();
    document_rail_->hide();
    view_->setFocus(Qt::OtherFocusReason);
  } else {
    workspace_panel_->setVisible(workspace_before_focus_);
    document_rail_->setVisible(rail_before_focus_);
    if (!sizes_before_focus_.isEmpty()) {
      splitter_->setSizes(sizes_before_focus_);
    }
  }
}

void ObservatoryWindow::JumpSection(int delta) {
  if (view_ == nullptr || delta == 0) {
    return;
  }
  const QVector<MarkdownHeading>& outline = view_->outline();
  if (outline.isEmpty()) {
    return;
  }
  const int scroll = view_->ScrollPosition();
  int current = -1;
  for (int i = 0; i < outline.size(); ++i) {
    const std::optional<int> top = view_->AnchorY(outline[i].anchor);
    if (top.has_value() && *top <= scroll + 24) {
      current = i;
    }
  }
  int target = 0;
  if (current < 0) {
    target = delta > 0 ? 0 : outline.size() - 1;
  } else {
    target = qBound(0, current + delta, outline.size() - 1);
  }
  view_->ScrollToAnchor(outline[target].anchor);
}

void ObservatoryWindow::ScrollBy(int delta) {
  if (view_ != nullptr) {
    view_->SetScrollPosition(view_->ScrollPosition() + delta);
  }
}

bool ObservatoryWindow::IsTyping() const {
  return qobject_cast<QLineEdit*>(QApplication::focusWidget()) != nullptr;
}

void ObservatoryWindow::HandleEscape() {
  if (palette_ != nullptr && palette_->is_open()) {
    palette_->Close();
    return;
  }
  if (focus_mode_) {
    SetFocusMode(false);
  }
}

void ObservatoryWindow::OpenRelative(int delta) {
  if (model_ == nullptr || model_->count() == 0 || delta == 0) {
    return;
  }
  const int count = model_->count();
  const int current = current_index_ < 0 ? 0 : current_index_;
  const int next = ((current + delta) % count + count) % count;
  OpenDocument(next);
}

bool ObservatoryWindow::OpenWorkspace(const QString& root) {
  const QDir dir(root);
  if (!dir.exists() || !model_->Load(dir.absolutePath())) {
    view_->ShowPlaceholder(tr("Workspace folder not found."));
    return false;
  }
  title_bar_->SetRootText(QDir::toNativeSeparators(model_->root()));
  current_index_ = -1;
  if (model_->count() == 0) {
    title_bar_->SetDocumentText(QString());
    breadcrumb_->clear();
    view_->ShowPlaceholder(tr("No Markdown files in this folder."));
    document_rail_->SetDocument(QString(), {}, {}, {});
    return true;
  }
  return OpenDocument(0);
}

bool ObservatoryWindow::OpenDocument(int index) {
  if (index < 0 || index >= model_->count()) {
    return false;
  }
  SaveCurrentPosition(true);
  current_index_ = index;
  current_percent_ = 0;
  resume_on_render_ = true;
  const WorkspaceDocument& document = model_->documents()[index];
  title_bar_->SetDocumentText(
      QStringLiteral("%1 / %2")
          .arg(index + 1, 2, 10, QLatin1Char('0'))
          .arg(model_->count(), 2, 10, QLatin1Char('0')));
  breadcrumb_->setText(document.relative_path);
  breadcrumb_->setToolTip(document.absolute_path);
  workspace_panel_->SetCurrentIndex(index);
  ShowResumeBar(false, 0);
  document_rail_->Clear();
  document_rail_->SetDocument(document.title, {}, {}, {});
  if (!view_->LoadFile(document.absolute_path)) {
    view_->ShowPlaceholder(tr("Could not open this document."));
    return false;
  }
  return true;
}

void ObservatoryWindow::OnRenderFinished() {
  if (current_index_ < 0 || current_index_ >= model_->count()) {
    return;
  }
  const WorkspaceDocument& document = model_->documents()[current_index_];
  QVector<int> heading_positions;
  const QVector<MarkdownHeading>& outline = view_->outline();
  heading_positions.reserve(outline.size());
  for (const MarkdownHeading& heading : outline) {
    const std::optional<int> y = view_->AnchorY(heading.anchor);
    heading_positions.push_back(y.has_value() ? *y : -1);
  }
  document_rail_->SetDocument(document.title, view_->blocks(), outline,
                              heading_positions);

  const int target = document.scroll;
  if (target > 4) {
    view_->SetScrollPosition(target);
    UpdateProgressFromScroll(view_->ScrollPosition());
    if (resume_on_render_ && current_percent_ > 0) {
      ShowResumeBar(true, current_percent_);
    }
  } else {
    view_->SetScrollPosition(0);
    UpdateProgressFromScroll(0);
    ShowResumeBar(false, 0);
  }
  resume_on_render_ = false;
}

void ObservatoryWindow::UpdateProgressFromScroll(int y) {
  if (current_index_ < 0 || current_index_ >= model_->count()) {
    return;
  }
  const int scroll = qMax(0, y);
  const int document_height = qMax(1, view_->DocumentHeight());
  const int viewport_height = qMax(1, view_->height());
  const int percent = ComputeProgress(scroll, document_height, viewport_height);
  current_percent_ = percent;
  model_->SetPosition(current_index_, scroll, percent, false);
  document_rail_->SetScroll(scroll, document_height, viewport_height);
}

void ObservatoryWindow::SaveCurrentPosition(bool persist) {
  if (current_index_ < 0 || current_index_ >= model_->count()) {
    return;
  }
  model_->SetPosition(current_index_, view_->ScrollPosition(), current_percent_,
                      persist);
}

void ObservatoryWindow::ShowResumeBar(bool visible, int percent) {
  resume_bar_->setVisible(visible);
  if (visible) {
    resume_text_->setText(tr("Resumed at %1%").arg(percent));
  }
}

void ObservatoryWindow::ApplyTheme(bool dark) {
  dark_ = dark;
  // Application-level palette updates every child, including the custom
  // painted rails that resolve colors from palette() directly.
  qApp->setPalette(AppPalette(dark));
  setPalette(AppPalette(dark));
  setStyleSheet(AppStyleSheet(dark));
  // QWindowKit reparents the title bar into the non-client host, so it no
  // longer inherits the window's palette or style sheet. Style it directly.
  if (title_bar_ != nullptr) {
    const QPalette title_palette = AppPalette(dark);
    title_bar_->setPalette(title_palette);
    title_bar_->setStyleSheet(AppStyleSheet(dark));
    const QList<QWidget*> children = title_bar_->findChildren<QWidget*>();
    for (QWidget* child : children) {
      child->setPalette(title_palette);
    }
    title_bar_->update();
    title_bar_->repaint();
    if (title_bar_->window() != nullptr &&
        title_bar_->window()->windowHandle() != nullptr) {
      title_bar_->window()->windowHandle()->requestUpdate();
    }
    // The non-client host keeps its first frame after a runtime theme change.
    // A hide/show makes it re-create the native chrome with the new palette.
    if (isVisible()) {
      title_bar_->setVisible(false);
      title_bar_->setVisible(true);
    }
  }
  if (window_agent_ != nullptr) {
    window_agent_->setWindowAttribute(QStringLiteral("dark-mode"), dark);
  }
  if (palette_ != nullptr) {
    palette_->setPalette(AppPalette(dark));
    palette_->SetDark(dark);
  }
  if (view_ != nullptr) {
    view_->SetTheme(dark ? MarkdownTheme::Dark : MarkdownTheme::Light);
  }
  if (workspace_panel_ != nullptr && model_ != nullptr) {
    // Group rows store an explicit foreground color; rebuild them under the
    // new palette.
    workspace_panel_->SetModel(model_);
    workspace_panel_->SetCurrentIndex(current_index_);
  }
  QSettings().setValue(QStringLiteral("appearance/dark"), dark);
  update();
}

void ObservatoryWindow::changeEvent(QEvent* event) {
  if (event->type() == QEvent::WindowStateChange && title_bar_ != nullptr) {
    title_bar_->SetMaximized(isMaximized());
  }
  QWidget::changeEvent(event);
}

void ObservatoryWindow::closeEvent(QCloseEvent* event) {
  SaveCurrentPosition(true);
  if (model_ != nullptr) {
    model_->Flush();
  }
  QWidget::closeEvent(event);
}

void ObservatoryWindow::showEvent(QShowEvent* event) {
  QWidget::showEvent(event);
  // QWindowKit moves the title bar into its non-client host while showing,
  // which drops the palette applied in the constructor. Re-apply once the
  // event loop has settled.
  QTimer::singleShot(0, this, [this] { ApplyTheme(dark_); });
}
