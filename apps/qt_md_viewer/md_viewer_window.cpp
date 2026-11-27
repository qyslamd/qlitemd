// clang-format off
#include "md_viewer_window.h"

// Must precede windows.h: winuser.h defines FindText as FindTextA/W.
#include "markdown_view.h"

#include "markdown_render_context.h"

#include <fluenttitlebar.h>
#include <fluentui3style.h>

#include <QWKWidgets/widgetwindowagent.h>

#include <optional>

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QColor>
#include <QDateTime>
#include <QDebug>
#include <QDesktopServices>
#include <QDialog>
#include <QDir>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QEasingCurve>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QFont>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QIcon>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QOperatingSystemVersion>
#include <QPalette>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPrinter>
#include <QProgressBar>
#include <QPushButton>
#include <QResizeEvent>
#include <QSet>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSize>
#include <QSizePolicy>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QVariantAnimation>
#include <QWheelEvent>
#include <QWindow>

#ifdef Q_OS_WIN
#include <windows.h>
// winuser.h maps FindText to FindTextA/W; MarkdownView's method is not that.
#undef FindText
#endif
// clang-format on

#include "activity_bar.h"
#include "bookmarks.h"
#include "chrome_theme.h"
#include "document_index.h"
#include "document_tab_bar.h"
#include "image_lightbox.h"
#include "markdown_outline_widget.h"
#include "md_viewer_settings.h"
#include "quick_open.h"
#include "reader_appearance_popup.h"
#include "reader_page.h"
#include "reading_positions.h"
#include "render_status_panel.h"
#include "search_controller.h"
#include "session_controller.h"
#include "settings_panel.h"
#include "side_panel.h"
#include "text_search.h"
#include "theme_icons.h"
#include "top_tool_bar.h"
#include "welcome_view.h"

namespace {

// 标题栏标签后的空白：拖动窗口，双击最大化/还原
class WindowDragArea final : public QWidget {
 public:
  using QWidget::QWidget;

 protected:
  void mousePressEvent(QMouseEvent* event) override {
    if (event->button() == Qt::LeftButton && window() != nullptr) {
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
      if (window()->windowHandle() != nullptr) {
        window()->windowHandle()->startSystemMove();
        return;
      }
#endif
    }
    QWidget::mousePressEvent(event);
  }

  void mouseDoubleClickEvent(QMouseEvent* event) override {
    if (event->button() == Qt::LeftButton && window() != nullptr) {
      if (window()->isMaximized()) {
        window()->showNormal();
      } else {
        window()->showMaximized();
      }
      return;
    }
    QWidget::mouseDoubleClickEvent(event);
  }
};

// 超过这个大小才提示；严格大于
constexpr qint64 kLargeDocumentBytes = 20 * 1024 * 1024;
// 快速跳转空查询的总上限是 80；标题不设上限会把命令挤出去
constexpr int kMaxQuickOpenHeadings = 30;

#ifdef Q_OS_WIN
// 改 Qt::WindowStaysOnTopHint 会重建 HWND，QWK 的模糊等窗口属性得重来；
// 置顶用 SetWindowPos 即可（Gallery 同款）
void SetTopMost(QWidget* window, bool top_most) {
  SetWindowPos(reinterpret_cast<HWND>(window->winId()),
               top_most ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0,
               SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}
#else
void SetTopMost(QWidget* window, bool top_most) {
  window->setWindowFlag(Qt::WindowStaysOnTopHint, top_most);
  window->show();
}
#endif

// 拖放只认本地 Markdown 文件或目录；返回按拖入顺序过滤后的路径
QStringList DroppablePaths(const QMimeData* mime) {
  QStringList paths;
  if (mime == nullptr || !mime->hasUrls()) {
    return paths;
  }
  for (const QUrl& url : mime->urls()) {
    if (!url.isLocalFile()) {
      continue;
    }
    const QFileInfo info(url.toLocalFile());
    const QString suffix = info.suffix().toLower();
    if (info.isDir() ||
        (info.isFile() && (suffix == QStringLiteral("md") ||
                           suffix == QStringLiteral("markdown")))) {
      paths.append(info.absoluteFilePath());
    }
  }
  return paths;
}

QString LoadTextResource(const QString& path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    qWarning() << "md_viewer: cannot read resource" << path;
    return {};
  }
  return QString::fromUtf8(file.readAll());
}

class SourceDialog final : public QDialog {
  // 按钮文案用了 tr()：缺 Q_OBJECT 会把上下文错成 QObject
  Q_OBJECT

 public:
  SourceDialog(const QString& title, const QString& source,
               QWidget* parent = nullptr)
      : QDialog(parent) {
    setObjectName(QStringLiteral("imageSourceDialog"));
    setWindowTitle(title);
    resize(680, 500);

    auto* layout = new QVBoxLayout(this);
    auto* text = new QPlainTextEdit(this);
    text->setObjectName(QStringLiteral("imageSourceText"));
    text->setReadOnly(true);
    text->setPlainText(source);
    text->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    layout->addWidget(text, 1);

    auto* buttons = new QHBoxLayout;
    auto* copy = new QPushButton(tr("Copy"), this);
    copy->setObjectName(QStringLiteral("imageSourceCopy"));
    auto* close = new QPushButton(tr("Close"), this);
    close->setObjectName(QStringLiteral("imageSourceClose"));
    buttons->addStretch(1);
    buttons->addWidget(copy);
    buttons->addWidget(close);
    layout->addLayout(buttons);

    connect(copy, &QPushButton::clicked, this, [text] {
      QApplication::clipboard()->setText(text->toPlainText());
    });
    connect(close, &QPushButton::clicked, this, &QDialog::accept);
  }
};

}  // namespace

MdViewerWindow::MdViewerWindow(MdViewerSettings* settings, bool session_owner,
                               QWidget* parent)
    : QMainWindow(parent), settings_(settings) {
  Q_ASSERT(settings_ != nullptr);
  // 次窗口拿主窗口设置的只读副本，改动只影响自己、不落盘
  if (!session_owner) {
    owned_settings_ = std::make_unique<MdViewerSettings>(*settings_);
    owned_settings_->read_only = true;
    settings_ = owned_settings_.get();
  }
  session_controller_ =
      std::make_unique<SessionController>(settings_, session_owner);
  bookmark_controller_ = std::make_unique<BookmarkController>(settings_);
  bookmark_controller_->Load();
  position_controller_ = std::make_unique<ReadingPositionController>(settings_);
  position_controller_->Load();
  setAcceptDrops(true);
  createActions();
  initUi();
  initSignals();
  // 次窗口不继承主窗口的几何与侧栏状态；它只负责一次性阅读
  const QByteArray saved_geometry = session_controller_->SavedGeometry();
  if (session_controller_->IsOwner() && !saved_geometry.isEmpty()) {
    restoreGeometry(saved_geometry);
  }
  if (top_tool_bar_ != nullptr) {
    top_tool_bar_->ResetInvariants();
  }
  // 活动栏、侧栏与大纲不走 dock 状态，直接按设置恢复
  layout_ = session_controller_->SavedShellLayout();
  if (actions_.outline != nullptr) {
    actions_.outline->setChecked(layout_.outline_visible);
  }
  if (activity_bar_ != nullptr && side_panel_ != nullptr) {
    ApplySideView(layout_.view);
    ApplySidebarVisible(layout_.sidebar_visible);
    ApplySplitterSizes();
  }
  ShowWelcome();
  // 侧栏/欢迎页/快速跳转的图标初始为亮色套；暗色启动在这里同步
  RefreshThemeIcons();
}

// qApp->quit() 不会走 closeEvent，析构时补一次落盘
MdViewerWindow::~MdViewerWindow() { SaveSettings(); }

QVariant MdViewerWindow::OpenPathForTest(const QVariant& path) {
  OpenPath(path.toString());
  return path.toString();
}

QVariant MdViewerWindow::OpenPathReplaceForTest(const QVariant& path) {
  NavigateTo(path.toString(), QString(), /*record_history=*/true,
             OpenIntent::kReplace);
  return TabStateForTest(QVariant());
}

QVariant MdViewerWindow::RootDirForTest(const QVariant& unused) const {
  Q_UNUSED(unused);
  return root_dir_;
}

QVariant MdViewerWindow::TabSettingsForTest(const QVariant& index) const {
  const int i = index.toInt();
  if (i < 0 || i >= static_cast<int>(tabs_.size()) ||
      tabs_[i]->view == nullptr) {
    return QVariant();
  }
  MarkdownView* view = tabs_[i]->view;
  QVariantMap state;
  state.insert(QStringLiteral("code_line_numbers"), view->code_line_numbers());
  state.insert(QStringLiteral("formula_rendering"),
               view->formula_rendering_enabled());
  state.insert(QStringLiteral("mermaid_rendering"),
               view->mermaid_rendering_enabled());
  state.insert(QStringLiteral("persistent_cache"),
               view->persistent_render_cache_enabled());
  state.insert(QStringLiteral("admonition"),
               static_cast<int>(view->admonition_mode()));
  state.insert(QStringLiteral("formula_backend"),
               static_cast<int>(view->formula_backend()));
  state.insert(QStringLiteral("mermaid_backend"),
               static_cast<int>(view->mermaid_backend()));
  state.insert(QStringLiteral("cache_directory"),
               view->render_context().options().cache_directory);
  state.insert(QStringLiteral("theme"), static_cast<int>(view->theme()));
  return state;
}

QVariant MdViewerWindow::ActivateTabForTest(const QVariant& index) {
  ActivateTab(index.toInt());
  return active_tab_;
}

QVariant MdViewerWindow::TabStateForTest(const QVariant& unused) const {
  Q_UNUSED(unused);
  QVariantMap state;
  state.insert(QStringLiteral("count"), static_cast<int>(tabs_.size()));
  state.insert(QStringLiteral("active"), active_tab_);
  QVariantList paths;
  for (const auto& tab : tabs_) {
    paths.append(tab->path);
  }
  state.insert(QStringLiteral("paths"), paths);
  return state;
}

QVariant MdViewerWindow::MoveTabForTest(const QVariant& args) {
  const QVariantList list = args.toList();
  if (tab_bar_ == nullptr || list.size() != 2) {
    return false;
  }
  tab_bar_->moveTab(list.at(0).toInt(), list.at(1).toInt());
  return true;
}

DocumentTab* MdViewerWindow::CurrentTab() const {
  if (active_tab_ < 0 || active_tab_ >= static_cast<int>(tabs_.size())) {
    return nullptr;
  }
  return tabs_[active_tab_].get();
}

MarkdownView* MdViewerWindow::CurrentView() const {
  DocumentTab* tab = CurrentTab();
  return tab != nullptr ? tab->view : nullptr;
}

ReaderPage* MdViewerWindow::CurrentPage() const {
  DocumentTab* tab = CurrentTab();
  return tab != nullptr ? tab->page.get() : nullptr;
}

int MdViewerWindow::FindTabIndex(const QString& path) const {
  if (path.isEmpty()) {
    return 0;
  }
  for (int i = 1; i < static_cast<int>(tabs_.size()); ++i) {
    if (tabs_[i]->path == path) {
      return i;
    }
  }
  return -1;
}

DocumentTab* MdViewerWindow::CreateDocumentTab() {
  auto tab = std::make_unique<DocumentTab>();
  auto* view = new MarkdownView();
  tab->view = view;
  tab->page = std::make_unique<ReaderPage>(view, pages_);
  pages_->addWidget(tab->page.get());
  // 新标签继承当前标签的缩放（§5.1），再套一遍全局设置
  DocumentTab* active = CurrentTab();
  if (active != nullptr) {
    tab->zoom_percent = active->zoom_percent;
    if (active->view != nullptr) {
      // 缓存目录与诊断设置不在 settings_ 里，按当前视图复制
      view->SetRenderCacheDirectory(
          active->view->render_context().options().cache_directory);
      view->SetDiagnostics(active->view->diagnostics());
    }
  }
  if (tab->zoom_percent != 100) {
    view->SetZoomFactor(tab->zoom_percent / 100.0);
  }
  ApplyGlobalSettings(tab.get());
  ConnectTabSignals(tab.get());
  tabs_.push_back(std::move(tab));
  DocumentTab* created = tabs_.back().get();
  if (tab_bar_ != nullptr) {
    // path 在 ShowFile 里才赋值；图标由 UpdateTabTitle 拿到路径后补
    tab_bar_->addTab(created->path.isEmpty()
                         ? tr("New Tab")
                         : QFileInfo(created->path).fileName());
    tab_bar_->setTabToolTip(
        tab_bar_->count() - 1,
        created->path.isEmpty()
            ? tr("New Tab")
            : DocumentDisplayTitle(root_dir_, created->path));
  }
  return created;
}

void MdViewerWindow::ClearDocumentTabs() {
  if (tab_bar_ != nullptr) {
    const QSignalBlocker blocker(tab_bar_);
    while (tab_bar_->count() > 1) {
      tab_bar_->removeTab(tab_bar_->count() - 1);
    }
  }
  for (int i = static_cast<int>(tabs_.size()) - 1; i >= 1; --i) {
    if (tabs_[i]->page != nullptr) {
      pages_->removeWidget(tabs_[i]->page.get());
    }
    // 立即析构：排队中的渲染信号不能带着已析构的 DocumentTab 指针进来
    tabs_.erase(tabs_.begin() + i);
  }
  active_tab_ = 0;
}

void MdViewerWindow::ApplyGlobalSettings(DocumentTab* tab) {
  if (tab == nullptr || tab->view == nullptr || settings_ == nullptr) {
    return;
  }
  MarkdownView* view = tab->view;
  view->SetTheme(settings_->dark_theme ? MarkdownTheme::Dark
                                       : MarkdownTheme::Light);
  view->SetCodeLineNumbers(settings_->code_line_numbers);
  view->SetAdmonitionMode(settings_->admonition_mode);
  view->SetMermaidBackend(settings_->mermaid_backend);
  view->SetFormulaBackend(settings_->formula_backend);
  view->SetFormulaRenderingEnabled(settings_->formula_rendering);
  view->SetMermaidRenderingEnabled(settings_->mermaid_rendering);
  view->SetPersistentRenderCacheEnabled(settings_->persistent_cache);
  // 阅读外观是追加层；AppendThemeStyleSheet 幂等，重复调用不会堆叠
  const bool prototype = settings_->reading_style == ReadingStyle::kPrototype;
  const ReaderPreferences preferences = CurrentReaderPreferences();
  view->AppendThemeStyleSheet(MarkdownTheme::Light,
                              ReaderAppearanceCss(preferences, prototype));
  view->AppendThemeStyleSheet(MarkdownTheme::Dark,
                              ReaderAppearanceCss(preferences, prototype));
  // 样式表与页面底色也要跟着新标签走，否则第二个标签用库默认外观
  ApplyReadingChromeTo(view);
}

void MdViewerWindow::ConnectTabSignals(DocumentTab* tab) {
  MarkdownView* view = tab->view;
  if (view == nullptr) {
    return;
  }
  connect(view, &MarkdownView::outlineChanged, this, [this, tab] {
    tab->outline = tab->view->outline();
    if (tab == CurrentTab()) {
      outline_->SetHeadings(tab->outline);
    }
  });
  connect(view, &MarkdownView::zoomChanged, this, [this, tab](qreal factor) {
    tab->zoom_percent = qRound(factor * 100.0);
    if (tab != CurrentTab()) {
      return;
    }
    UpdateZoomLabel(factor);
    // 缩放有 150ms 合并窗口，等重排完成后再量进度
    QTimer::singleShot(250, this,
                       &MdViewerWindow::ScheduleReadingProgressRefresh);
  });
  connect(view, &MarkdownView::renderStatusChanged, this, [this, tab] {
    if (tab == CurrentTab()) {
      ReportRenderStatus();
    }
  });
  connect(view, &MarkdownView::documentReloaded, this, [this, tab] {
    if (tab == CurrentTab()) {
      statusBar()->showMessage(tr("File updated, reloaded"), 3000);
    }
  });
  connect(
      view, &MarkdownView::renderProgress, this,
      [this, tab](quint64 request_id, int completed, int total, int in_flight) {
        Q_UNUSED(in_flight);
        tab->render_request = request_id;
        SetTabLoading(tab, true);
        if (tab != CurrentTab()) {
          return;
        }
        if (total > 0) {
          render_progress_->setRange(0, total);
          render_progress_->setValue(completed);
          render_progress_->setFormat(tr("Render %v/%m"));
          render_progress_->show();
        } else {
          render_progress_->hide();
        }
      });
  connect(view, &MarkdownView::renderFinished, this,
          [this, tab, view](quint64) {
            tab->render_request = 0;
            SetTabLoading(tab, false);
            // 锚点与滚动恢复对后台标签也要做，否则切回来时位置丢了
            if (!tab->pending_fragment.isEmpty()) {
              const QString fragment = tab->pending_fragment;
              tab->pending_fragment.clear();
              tab->pending_restore = false;
              tab->pending_restore_path.clear();
              if (fragment.isEmpty()) {
                view->SetScrollPosition(0);
              } else {
                view->ScrollToAnchor(fragment);
              }
            } else if (tab == CurrentTab() && pending_search_hit_.has_value()) {
              const PendingJump jump = *pending_search_hit_;
              pending_search_hit_.reset();
              tab->pending_restore = false;
              tab->pending_restore_path.clear();
              if (jump.path == view->current_file()) {
                JumpToSearchHit(jump.ordinal, jump.query);
              }
            } else if (tab->pending_restore &&
                       tab->pending_restore_path == view->current_file()) {
              const int scroll = tab->scroll_position;
              tab->pending_restore = false;
              tab->pending_restore_path.clear();
              view->SetScrollPosition(scroll);
            } else {
              tab->pending_restore = false;
              tab->pending_restore_path.clear();
            }
            if (tab != CurrentTab()) {
              return;
            }
            render_progress_->hide();
            render_progress_->setRange(0, 1);
            render_progress_->setValue(0);
            UpdateReadingProgress(view->ScrollPosition());
            UpdateReadingTime();
          });
  connect(view, &MarkdownView::codeCopied, this, [this, tab] {
    if (tab == CurrentTab()) {
      statusBar()->showMessage(tr("Code block copied"), 2000);
    }
  });
  connect(view, &MarkdownView::imageClicked, this,
          [this, tab](const QUrl& url) {
            if (tab == CurrentTab()) {
              OpenImage(url);
            }
          });
  connect(view, &MarkdownView::imageContextMenuRequested, this,
          [this, tab](const QPoint& pos, const QUrl& url) {
            if (tab == CurrentTab()) {
              ShowImageContextMenu(pos, url);
            }
          });
  connect(view, &MarkdownView::resourceLoadFailed, this,
          [this, tab](const QUrl& url) {
            if (tab != CurrentTab()) {
              return;
            }
            const QString label = url.isLocalFile()
                                      ? QFileInfo(url.toLocalFile()).fileName()
                                      : url.toString();
            statusBar()->showMessage(tr("Image failed to load: %1").arg(label),
                                     5000);
          });
  connect(view, &MarkdownView::viewportScrolled, this,
          [this, tab, view](int y) {
            if (tab != CurrentTab()) {
              return;
            }
            constexpr int kAnchorSlackPx = 32;
            QString current;
            for (const MarkdownHeading& heading : view->outline()) {
              const std::optional<int> anchor_y = view->AnchorY(heading.anchor);
              if (!anchor_y.has_value()) {
                continue;
              }
              if (*anchor_y > y + kAnchorSlackPx) {
                break;
              }
              current = heading.anchor;
            }
            outline_->SetCurrentAnchor(current);
            UpdateReadingProgress(y);
          });
  connect(view, &MarkdownView::loadFailed, this,
          [this, tab](const QString& path, const QString& reason) {
            SetTabLoading(tab, false);
            if (tab != CurrentTab()) {
              return;
            }
            statusBar()->showMessage(
                tr("Failed to open: %1 (%2)")
                    .arg(QFileInfo(path).fileName(), reason),
                5000);
          });
  connect(
      view, &MarkdownView::linkClicked, this,
      [this, tab, view](const QUrl& url) {
        if (tab != CurrentTab()) {
          return;
        }
        const QString fragment = url.fragment();
        if (!fragment.isEmpty()) {
          const QUrl no_frag = url.adjusted(QUrl::RemoveFragment);
          const QUrl here =
              view->has_document()
                  ? QUrl::fromLocalFile(
                        QFileInfo(view->current_file()).absolutePath() +
                        QLatin1Char('/'))
                  : QUrl();
          if (no_frag.isEmpty() || no_frag == here) {
            NavigateTo(view->current_file(), fragment, /*record_history=*/true);
            return;
          }
        }
        const QString path = url.isLocalFile() ? url.toLocalFile() : QString();
        if (!path.isEmpty() &&
            (path.endsWith(QStringLiteral(".md"), Qt::CaseInsensitive) ||
             path.endsWith(QStringLiteral(".markdown"), Qt::CaseInsensitive))) {
          NavigateTo(path, fragment, /*record_history=*/true);
          return;
        }
        const bool web_link = url.scheme() == QLatin1String("http") ||
                              url.scheme() == QLatin1String("https") ||
                              url.scheme() == QLatin1String("mailto");
        if (web_link || QFileInfo(path).isFile()) {
          QDesktopServices::openUrl(url);
        }
      });
  if (tab->page != nullptr) {
    connect(tab->page->back_to_top(), &QToolButton::clicked, this,
            &MdViewerWindow::ScrollToTop);
  }
}

void MdViewerWindow::CaptureGlobalSettings(DocumentTab* tab) {
  if (tab == nullptr || tab->view == nullptr || settings_ == nullptr) {
    return;
  }
  MarkdownView* view = tab->view;
  settings_->dark_theme = view->theme() == MarkdownTheme::Dark;
  settings_->code_line_numbers = view->code_line_numbers();
  settings_->admonition_mode = view->admonition_mode();
  settings_->mermaid_backend = view->mermaid_backend();
  settings_->formula_backend = view->formula_backend();
  settings_->formula_rendering = view->formula_rendering_enabled();
  settings_->mermaid_rendering = view->mermaid_rendering_enabled();
  settings_->persistent_cache = view->persistent_render_cache_enabled();
}

void MdViewerWindow::ActivateTab(int index) {
  if (index < 0 || index >= static_cast<int>(tabs_.size())) {
    return;
  }
  if (index == active_tab_) {
    return;
  }
  // 切走前保存当前标签的滚动与大纲，并把全局设置收回到 settings_
  DocumentTab* previous = CurrentTab();
  CaptureGlobalSettings(previous);
  if (previous != nullptr && previous->view != nullptr) {
    previous->view->ClearSelection();
  }
  if (previous != nullptr && previous->view != nullptr &&
      previous->view->has_document() && !previous->view->is_rendering()) {
    previous->scroll_position = previous->view->ScrollPosition();
    previous->outline = previous->view->outline();
    previous->zoom_percent = qRound(previous->view->zoom() * 100.0);
  }
  active_tab_ = index;
  ShowActiveTab();
}

void MdViewerWindow::ShowActiveTab() {
  if (scroll_animation_ != nullptr) {
    scroll_animation_->stop();
  }
  DocumentTab* tab = CurrentTab();
  if (tab == nullptr) {
    return;
  }
  QWidget* page =
      tab->page != nullptr ? static_cast<QWidget*>(tab->page.get()) : welcome_;
  pages_->setCurrentWidget(page);
  if (tab->view != nullptr) {
    ApplyGlobalSettings(tab);
    if (settings_panel_ != nullptr) {
      settings_panel_->SetView(tab->view);
    }
    if (render_status_ != nullptr) {
      render_status_->SetView(tab->view);
    }
    outline_->SetHeadings(tab->outline.isEmpty() ? tab->view->outline()
                                                 : tab->outline);
    UpdateZoomLabel(tab->zoom_percent / 100.0);
  }
  if (tab_bar_ != nullptr) {
    const QSignalBlocker blocker(tab_bar_);
    if (active_tab_ < tab_bar_->count()) {
      tab_bar_->setCurrentIndex(active_tab_);
    }
    UpdateTabTitle(tab);
  }
  UpdateShellForActiveTab();
}

void MdViewerWindow::WriteTabSession() {
  QVector<QString> paths;
  paths.reserve(static_cast<int>(tabs_.size()));
  for (const auto& tab : tabs_) {
    paths.append(tab->path);
  }
  session_controller_->SaveTabs(paths, active_tab_);
}

void MdViewerWindow::SaveTabSession() {
  // 恢复期间不把半成品写回；恢复结束由 WriteTabSession 强制写一次
  if (session_controller_->IsRestoring()) {
    return;
  }
  WriteTabSession();
}

void MdViewerWindow::CloseTab(int index) {
  if (index <= 0 || index >= static_cast<int>(tabs_.size())) {
    return;
  }
  const bool was_active = index == active_tab_;
  DocumentTab* tab = tabs_[index].get();
  if (!tab->path.isEmpty()) {
    closed_tabs_.removeAll(tab->path);
    closed_tabs_.prepend(tab->path);
    while (closed_tabs_.size() > 10) {
      closed_tabs_.removeLast();
    }
  }
  if (tab->page != nullptr) {
    pages_->removeWidget(tab->page.get());
  }
  // DocumentTab 析构时删除 ReaderPage（连带其子视图）
  tabs_.erase(tabs_.begin() + index);
  if (tab_bar_ != nullptr) {
    const QSignalBlocker blocker(tab_bar_);
    if (index < tab_bar_->count()) {
      tab_bar_->removeTab(index);
    }
  }
  if (index < active_tab_) {
    --active_tab_;
  } else if (was_active) {
    // 关掉当前标签：落到右邻居，没有就落回左邻居
    active_tab_ = qMin(index, static_cast<int>(tabs_.size()) - 1);
  }
  if (was_active) {
    ShowActiveTab();
  } else {
    UpdateShellForActiveTab();
  }
  SaveTabSession();
}

void MdViewerWindow::ReloadTab(int index) {
  if (index <= 0 || index >= static_cast<int>(tabs_.size())) {
    return;
  }
  DocumentTab* tab = tabs_[index].get();
  if (tab == nullptr || tab->view == nullptr || tab->path.isEmpty() ||
      tab->view->is_rendering()) {
    return;
  }
  // 重新加载保留当前阅读位置，由 renderFinished 恢复
  tab->pending_restore_path = tab->path;
  tab->scroll_position = tab->view->ScrollPosition();
  tab->pending_restore = true;
  SetTabLoading(tab, true);
  if (tab->view->LoadFile(tab->path)) {
    tab->view->ShowPlaceholder(tr("Re-rendering…"));
  } else {
    SetTabLoading(tab, false);
  }
}

void MdViewerWindow::ReopenClosedTab() {
  while (!closed_tabs_.isEmpty()) {
    const QString path = closed_tabs_.takeFirst();
    if (QFileInfo(path).isFile()) {
      NavigateTo(path, QString(), /*record_history=*/true, OpenIntent::kNewTab);
      return;
    }
  }
  statusBar()->showMessage(tr("No closed tab to reopen"), 3000);
}

void MdViewerWindow::OnTabMoved(int from, int to) {
  if (from <= 0 || to <= 0 || from >= static_cast<int>(tabs_.size()) ||
      to >= static_cast<int>(tabs_.size())) {
    // 欢迎标签固定第 0 位：把标签条上的移动撤回去
    if (tab_bar_ != nullptr && from != to) {
      const QSignalBlocker blocker(tab_bar_);
      tab_bar_->moveTab(to, from);
    }
    return;
  }
  DocumentTab* active = CurrentTab();
  std::unique_ptr<DocumentTab> moved = std::move(tabs_[from]);
  tabs_.erase(tabs_.begin() + from);
  tabs_.insert(tabs_.begin() + to, std::move(moved));
  for (int i = 0; i < static_cast<int>(tabs_.size()); ++i) {
    if (tabs_[i].get() == active) {
      active_tab_ = i;
      break;
    }
  }
  // 拖拽期间 QTabBar 的选中项可能落到了别的下标，按模型拉回来
  if (tab_bar_ != nullptr) {
    const QSignalBlocker blocker(tab_bar_);
    tab_bar_->setCurrentIndex(active_tab_);
  }
}

void MdViewerWindow::SetTabLoading(DocumentTab* tab, bool loading) {
  if (tab_bar_ == nullptr || tab == nullptr) {
    return;
  }
  for (int i = 0; i < static_cast<int>(tabs_.size()); ++i) {
    if (tabs_[i].get() == tab) {
      tab_bar_->SetTabLoading(i, loading);
      break;
    }
  }
  UpdateFindControls();
}

void MdViewerWindow::UpdateTabTitle(DocumentTab* tab) {
  if (tab == nullptr || tab_bar_ == nullptr) {
    return;
  }
  int index = -1;
  for (int i = 0; i < static_cast<int>(tabs_.size()); ++i) {
    if (tabs_[i].get() == tab) {
      index = i;
      break;
    }
  }
  if (index < 0 || index >= tab_bar_->count()) {
    return;
  }
  const bool welcome = tab->path.isEmpty();
  tab_bar_->setTabText(
      index, welcome ? tr("Welcome") : QFileInfo(tab->path).fileName());
  tab_bar_->setTabToolTip(index,
                          welcome ? tr("Welcome Page")
                                  : DocumentDisplayTitle(root_dir_, tab->path));
  // 欢迎标签用彩色应用图标，文档标签用当前主题的单色文件图标
  tab_bar_->setTabIcon(
      index, welcome ? QIcon(QStringLiteral(":/resources/svg/icon.svg"))
                     : ThemeIcon(QStringLiteral("file"), ThemeDark()));
}

void MdViewerWindow::UpdateShellForActiveTab() {
  // 计数属于上一个文档的查找会话，换标签/换文档后不能留在工具栏上。
  ClearFindCount();
  DocumentTab* tab = CurrentTab();
  if (actions_.close_tab != nullptr) {
    actions_.close_tab->setEnabled(active_tab_ > 0);
  }
  if (tab_bar_ != nullptr) {
    const QSignalBlocker blocker(tab_bar_);
    if (active_tab_ < tab_bar_->count()) {
      tab_bar_->setCurrentIndex(active_tab_);
    }
    UpdateTabTitle(tab);
  }
  const bool welcome = tab == nullptr || tab->path.isEmpty();
  if (bar_stack_ != nullptr) {
    bar_stack_->setCurrentIndex(welcome ? 1 : 0);
  }
  if (welcome_bar_ != nullptr) {
    welcome_bar_->SetWorkspaceName(root_dir_);
  }
  UpdateFindControls();
  UpdateOutlineVisibility();
  if (welcome) {
    setWindowTitle(tr("Markdown Viewer"));
    if (welcome_ != nullptr) {
      welcome_->SetRecentFiles(settings_->recent_files, root_dir_);
    }
    if (document_bar_ != nullptr) {
      document_bar_->SetPath(QString(), QString());
    }
    if (side_panel_ != nullptr) {
      side_panel_->SetCurrentFile(QString());
    }
  } else {
    setWindowTitle(DocumentDisplayTitle(root_dir_, tab->path));
    if (document_bar_ != nullptr) {
      document_bar_->SetPath(DocumentDisplayTitle(root_dir_, tab->path),
                             QDir::toNativeSeparators(tab->path));
    }
    if (side_panel_ != nullptr) {
      side_panel_->SetCurrentFile(tab->path);
    }
  }
  if (tab_count_label_ != nullptr) {
    tab_count_label_->setText(
        tr("%1 tabs").arg(static_cast<int>(tabs_.size())));
  }
  UpdateBookmarkUi();
  UpdateHistoryActions();
  UpdateReadingTime();
  if (CurrentView() != nullptr) {
    UpdateReadingProgress(CurrentView()->ScrollPosition());
  }
}

void MdViewerWindow::createActions() {
  actions_.open_file = new QAction(tr("Open File…"), this);
  actions_.open_file->setShortcut(QKeySequence::Open);
  actions_.open_dir = new QAction(tr("Open Folder…"), this);
  actions_.close_tab = new QAction(tr("Close Tab"), this);
  actions_.close_tab->setObjectName(QStringLiteral("closeTabAction"));
  actions_.close_tab->setShortcut(QKeySequence(QStringLiteral("Ctrl+W")));
  actions_.reopen_tab = new QAction(tr("Reopen Closed Tab"), this);
  actions_.reopen_tab->setObjectName(QStringLiteral("reopenTabAction"));
  actions_.reopen_tab->setShortcut(
      QKeySequence(QStringLiteral("Ctrl+Shift+T")));
  actions_.new_window = new QAction(tr("New Window"), this);
  actions_.new_window->setObjectName(QStringLiteral("newWindowAction"));
  actions_.new_window->setShortcut(
      QKeySequence(QStringLiteral("Ctrl+Shift+N")));
  actions_.unload_workspace = new QAction(tr("Unload Workspace"), this);
  actions_.unload_workspace->setObjectName(
      QStringLiteral("unloadWorkspaceAction"));
  actions_.unload_workspace->setEnabled(false);
  actions_.quit = new QAction(tr("Quit"), this);
  actions_.quit->setShortcut(QKeySequence::Quit);
  actions_.about = new QAction(tr("About"), this);
  actions_.export_html = new QAction(tr("Export HTML…"), this);
  actions_.export_html->setObjectName(QStringLiteral("exportHtmlAction"));
  actions_.export_pdf = new QAction(tr("Export PDF…"), this);

  // 两个搜索入口：Ctrl+F 查当前文档，Ctrl+Shift+F 搜整个工作区。
  // Ctrl+K 退休，不再绑定任何搜索
  actions_.find = new QAction(tr("Find in Current Document"), this);
  actions_.find->setShortcut(QKeySequence(QKeySequence::Find));
  actions_.find->setToolTip(tr("Find in Current Document (Ctrl+F)"));
  actions_.workspace_search = new QAction(tr("Search Workspace"), this);
  actions_.workspace_search->setObjectName(
      QStringLiteral("workspaceSearchAction"));
  actions_.workspace_search->setShortcut(
      QKeySequence(QStringLiteral("Ctrl+Shift+F")));
  actions_.workspace_search->setToolTip(
      tr("Search workspace files and content (Ctrl+Shift+F)"));

  // 活动栏的四个视图与大纲开关
  actions_.view_documents = new QAction(tr("Documents View"), this);
  actions_.view_documents->setShortcut(
      QKeySequence(QStringLiteral("Ctrl+Shift+E")));
  actions_.view_bookmarks = new QAction(tr("Bookmarks View"), this);
  actions_.view_bookmarks->setShortcut(
      QKeySequence(QStringLiteral("Ctrl+Shift+B")));
  actions_.view_render_status = new QAction(tr("Render Status View"), this);
  actions_.view_render_status->setShortcut(
      QKeySequence(QStringLiteral("Ctrl+Shift+R")));
  actions_.outline = new QAction(tr("Outline"), this);
  actions_.outline->setObjectName(QStringLiteral("outlineAction"));
  actions_.outline->setCheckable(true);
  actions_.outline->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+O")));
  actions_.toggle_sidebar = new QAction(tr("Toggle Sidebar"), this);
  actions_.toggle_sidebar->setObjectName(QStringLiteral("toggleSidebarAction"));
  actions_.toggle_sidebar->setCheckable(true);
  actions_.toggle_sidebar->setChecked(true);
  actions_.toggle_sidebar->setShortcut(QKeySequence(QStringLiteral("Ctrl+B")));
}

void MdViewerWindow::initUi() {
  setObjectName(QStringLiteral("mdViewerWindow"));
  setWindowTitle(tr("Markdown Viewer"));
  setMinimumWidth(600);
  resize(1100, 800);

  // 中心区：欢迎页固定第 0 页，文档标签各占一页
  pages_ = new QStackedWidget(this);
  pages_->setObjectName(QStringLiteral("readerContainer"));
  pages_->installEventFilter(this);
  welcome_ = new WelcomeView(pages_);
  welcome_->setObjectName(QStringLiteral("welcomeView"));
  pages_->addWidget(welcome_);

  // 文档条是窗口级控件：宿主是顶部工具条，横跨整窗，侧栏与大纲落在
  // 它下面。工具条里用页栈，切标签只切页，欢迎栏是第 1 页
  document_bar_ = new DocumentBar(this);
  welcome_bar_ = new WelcomeBar(this);
  bar_stack_ = new QStackedWidget(this);
  bar_stack_->setObjectName(QStringLiteral("barStack"));
  bar_stack_->addWidget(document_bar_);
  bar_stack_->addWidget(welcome_bar_);
  // 工具条不再是 QMainWindow 的 toolbar 区域，而是右列的第二行
  top_tool_bar_ = new TopToolBar(this);
  top_tool_bar_->addWidget(bar_stack_);

  // 欢迎标签：不是文档，但保留一个空视图给设置/状态面板做初始绑定
  auto welcome_tab = std::make_unique<DocumentTab>();
  welcome_tab->view = new MarkdownView(pages_);
  welcome_tab->view->hide();
  ApplyGlobalSettings(welcome_tab.get());
  tabs_.push_back(std::move(welcome_tab));

  scroll_animation_ = new QVariantAnimation(this);
  scroll_animation_->setDuration(220);
  scroll_animation_->setEasingCurve(QEasingCurve::OutCubic);
  connect(scroll_animation_, &QVariantAnimation::valueChanged, this,
          [this](const QVariant& value) {
            if (CurrentView() != nullptr) {
              CurrentView()->SetScrollPosition(value.toInt());
            }
          });

  // 活动栏 + 侧栏 + 正文 + 大纲：固定布局，不再使用 dock
  activity_bar_ = new ActivityBar(this);

  side_panel_ = new SidePanel(this);
  // 渲染状态页由宿主创建、SidePanel 提供带边距的宿主页
  render_status_ = new RenderStatusPanel(CurrentView());
  side_panel_->SetRenderStatusWidget(render_status_);

  // 大纲固定在正文右侧；标题行留一个收起按钮
  outline_column_ = new QWidget(this);
  outline_column_->setObjectName(QStringLiteral("outlineColumn"));
  outline_column_->setMinimumWidth(140);
  auto* outline_layout = new QVBoxLayout(outline_column_);
  outline_layout->setContentsMargins(0, 0, 0, 0);
  outline_layout->setSpacing(0);
  auto* outline_header = new QWidget(outline_column_);
  outline_header->setObjectName(QStringLiteral("outlineHeader"));
  auto* outline_header_layout = new QHBoxLayout(outline_header);
  outline_header_layout->setContentsMargins(10, 6, 6, 6);
  outline_header_layout->setSpacing(6);
  auto* outline_title = new QLabel(tr("Outline"), outline_header);
  outline_title->setObjectName(QStringLiteral("outlineTitle"));
  outline_header_layout->addWidget(outline_title);
  outline_header_layout->addStretch(1);
  outline_hide_button_ = new QToolButton(outline_header);
  outline_hide_button_->setObjectName(QStringLiteral("outlineHideButton"));
  outline_hide_button_->setAutoRaise(true);
  outline_hide_button_->setText(QStringLiteral("×"));
  outline_hide_button_->setToolTip(tr("Hide Outline (Ctrl+Shift+O)"));
  outline_header_layout->addWidget(outline_hide_button_);
  outline_ = new MarkdownOutlineWidget(outline_column_);
  outline_layout->addWidget(outline_header);
  outline_layout->addWidget(outline_, 1);

  // 外壳：左列（活动栏+侧栏）从窗口最上沿直通状态栏，右列承载标题栏与内容
  auto* center = new QWidget(this);
  center->setObjectName(QStringLiteral("mainColumns"));
  auto* columns = new QHBoxLayout(center);
  columns->setContentsMargins(0, 0, 0, 0);
  columns->setSpacing(0);

  left_shell_ = new QWidget(center);
  left_shell_->setObjectName(QStringLiteral("leftShell"));
  left_shell_->setMinimumWidth(42);
  auto* shell_left_layout = new QHBoxLayout(left_shell_);
  shell_left_layout->setContentsMargins(0, 0, 0, 0);
  shell_left_layout->setSpacing(0);
  shell_left_layout->addWidget(activity_bar_);
  shell_left_layout->addWidget(side_panel_, 1);

  right_shell_ = new QWidget(center);
  right_shell_->setObjectName(QStringLiteral("rightShell"));
  auto* right_layout = new QVBoxLayout(right_shell_);
  right_layout->setContentsMargins(0, 0, 0, 0);
  right_layout->setSpacing(0);

  // 标题栏不再经 FluentWindowFrame 的全宽 menuWidget，直接嵌进右列
  title_bar_ = new FluentTitleBar(this);
  // 强调色按钮只在标题栏内部改选中态并发信号，查看器没有消费方，隐藏
  for (QToolButton* button : title_bar_->accentButtons()) {
    button->setVisible(false);
  }
  // 与文档主题初始一致；标题栏据此选搜索图标等自身角色的配色
  title_bar_->setThemeDark(CurrentView()->theme() != MarkdownTheme::Light);
  right_layout->addWidget(title_bar_);
  right_layout->addWidget(top_tool_bar_);

  // 正文、大纲宽度可拖动；侧栏宽度归外壳 splitter 管
  main_splitter_ = new QSplitter(Qt::Horizontal, right_shell_);
  main_splitter_->setObjectName(QStringLiteral("mainSplitter"));
  main_splitter_->setChildrenCollapsible(false);
  main_splitter_->setHandleWidth(4);
  main_splitter_->addWidget(pages_);
  main_splitter_->addWidget(outline_column_);
  main_splitter_->setStretchFactor(0, 1);
  main_splitter_->setStretchFactor(1, 0);
  right_layout->addWidget(main_splitter_, 1);

  shell_splitter_ = new QSplitter(Qt::Horizontal, center);
  shell_splitter_->setObjectName(QStringLiteral("shellSplitter"));
  shell_splitter_->setChildrenCollapsible(false);
  shell_splitter_->setHandleWidth(4);
  shell_splitter_->addWidget(left_shell_);
  shell_splitter_->addWidget(right_shell_);
  shell_splitter_->setStretchFactor(0, 0);
  shell_splitter_->setStretchFactor(1, 1);
  columns->addWidget(shell_splitter_, 1);
  setCentralWidget(center);
  // 阅读外观弹层：改的是全局阅读参数，不做成 dock，也不占正文
  appearance_popup_ = new ReaderAppearancePopup(this);
  connect(appearance_popup_, &ReaderAppearancePopup::preferencesChanged, this,
          [this](const ReaderPreferences& preferences) {
            settings_->reading_font_size = preferences.font_size;
            settings_->reading_line_height_percent =
                preferences.line_height_percent;
            settings_->reading_measure_px = preferences.measure_px;
            session_controller_->SaveAll();
            ApplyReaderAppearance();
          });

  // 渲染设置：模型对话框，改动即时生效；关闭只是隐藏
  settings_panel_ = new SettingsPanel(CurrentView(), this);
  settings_panel_->SetWindowMaterial(settings_->window_material);
  settings_panel_->SetReadingStyle(settings_->reading_style);
  settings_panel_->SetTransparentReading(settings_->transparent_reading);
  settings_dialog_ = new SettingsDialog(settings_panel_, this);

  statusBar()->setObjectName(QStringLiteral("mainStatusBar"));
  statusBar()->setSizeGripEnabled(false);
  statusBar()->setFixedHeight(26);

  // 原型：绿点 + 就绪 | 当前文档完整路径 | 右侧 篇数 · 后端 · 缩放
  auto* left_widget = new QWidget(this);
  left_widget->setObjectName(QStringLiteral("statusLeftWidget"));
  auto* left_layout = new QHBoxLayout(left_widget);
  left_layout->setContentsMargins(0, 0, 0, 0);
  left_layout->setSpacing(14);
  auto* ready_widget = new QWidget(left_widget);
  ready_widget->setObjectName(QStringLiteral("statusReadyWidget"));
  auto* ready_layout = new QHBoxLayout(ready_widget);
  ready_layout->setContentsMargins(0, 0, 0, 0);
  ready_layout->setSpacing(5);
  auto* status_dot = new QLabel(ready_widget);
  status_dot->setObjectName(QStringLiteral("statusDotLabel"));
  status_dot->setPixmap(
      QIcon(QStringLiteral(":/resources/svg/status-dot.svg")).pixmap(8, 8));
  status_dot->setFixedSize(8, 8);
  ready_layout->addWidget(status_dot);
  status_ready_label_ = new QLabel(tr("Ready"), ready_widget);
  status_ready_label_->setObjectName(QStringLiteral("statusReadyLabel"));
  ready_layout->addWidget(status_ready_label_);
  left_layout->addWidget(ready_widget);

  statusBar()->addWidget(left_widget, 1);

  render_progress_ = new QProgressBar(this);
  render_progress_->setObjectName(QStringLiteral("renderProgressBar"));
  render_progress_->setTextVisible(true);
  render_progress_->setFormat(tr("Render %v/%m"));
  render_progress_->setFixedWidth(160);
  render_progress_->setToolTip(
      tr("Render task progress of the current document"));
  render_progress_->hide();
  tab_count_label_ = new QLabel(tr("1 tab"), this);
  tab_count_label_->setObjectName(QStringLiteral("tabCountLabel"));
  tab_count_label_->setToolTip(tr("Number of open tabs"));
  document_count_label_ = new QLabel(tr("0 documents"), this);
  document_count_label_->setObjectName(QStringLiteral("documentCountLabel"));
  document_count_label_->setToolTip(
      tr("Markdown documents in the current folder"));
  reading_time_label_ = new QLabel(this);
  reading_time_label_->setObjectName(QStringLiteral("readingTimeLabel"));
  reading_time_label_->setToolTip(
      tr("Estimated from source characters, about 500 chars/min"));
  backend_label_ =
      new QLabel(QStringLiteral(QLITEMD_BACKEND_LABEL)
                     .replace(QStringLiteral(" / "), QStringLiteral(" · ")),
                 this);
  backend_label_->setObjectName(QStringLiteral("backendLabel"));
  backend_label_->setToolTip(tr("Render backends of this build"));
  auto* stats_widget = new QWidget(this);
  stats_widget->setObjectName(QStringLiteral("statusStatsWidget"));
  auto* stats_layout = new QHBoxLayout(stats_widget);
  stats_layout->setContentsMargins(0, 0, 0, 0);
  stats_layout->setSpacing(14);
  stats_layout->addWidget(tab_count_label_);
  stats_layout->addWidget(document_count_label_);
  stats_layout->addWidget(reading_time_label_);
  stats_layout->addWidget(backend_label_);
  statusBar()->addPermanentWidget(render_progress_);
  statusBar()->addPermanentWidget(stats_widget);
  UpdateZoomLabel(CurrentView()->zoom());

  // 快速跳转：统一入口，Ctrl+P 与 VS Code 的快速打开同键
  const bool initial_dark = settings_->dark_theme;
  actions_.quick_open = new QAction(tr("Quick Open…"), this);
  actions_.quick_open->setObjectName(QStringLiteral("quickOpenAction"));
  actions_.quick_open->setIcon(
      ThemeIcon(QStringLiteral("search"), initial_dark));
  actions_.quick_open->setShortcut(QKeySequence(QStringLiteral("Ctrl+P")));
  actions_.quick_open->setToolTip(
      tr("Jump to file, heading or command (Ctrl+P)"));

  // 导航与缩放从工具条搬进标题栏；文件跳转和锚点跳转共用一个历史栈
  actions_.back = new QAction(tr("Back"), this);
  actions_.back->setObjectName(QStringLiteral("navBackAction"));
  actions_.back->setIcon(ThemeIcon(QStringLiteral("back"), initial_dark));
  actions_.back->setShortcut(QKeySequence(QStringLiteral("Ctrl+Alt+Left")));
  actions_.back->setToolTip(tr("Back (Ctrl+Alt+←)"));

  actions_.forward = new QAction(tr("Forward"), this);
  actions_.forward->setObjectName(QStringLiteral("navForwardAction"));
  actions_.forward->setIcon(ThemeIcon(QStringLiteral("forward"), initial_dark));
  actions_.forward->setShortcut(QKeySequence(QStringLiteral("Ctrl+Alt+Right")));
  actions_.forward->setToolTip(tr("Forward (Ctrl+Alt+→)"));

  // 缩放快捷键由 MarkdownView 内部持有，这里的按钮不再重复注册快捷键
  actions_.zoom_out = new QAction(QStringLiteral("−"), this);
  actions_.zoom_out->setObjectName(QStringLiteral("zoomOutAction"));
  actions_.zoom_out->setIcon(
      ThemeIcon(QStringLiteral("zoom-out"), initial_dark));
  actions_.zoom_out->setToolTip(tr("Zoom Out (Ctrl+-)"));

  actions_.zoom_reset = new QAction(QStringLiteral("100%"), this);
  actions_.zoom_reset->setObjectName(QStringLiteral("zoomResetAction"));
  actions_.zoom_reset->setIcon(
      ThemeIcon(QStringLiteral("zoom-reset"), initial_dark));
  actions_.zoom_reset->setToolTip(tr("Reset Zoom (Ctrl+0)"));

  actions_.settings = new QAction(tr("Settings…"), this);
  actions_.settings->setObjectName(QStringLiteral("settingsAction"));
  actions_.settings->setShortcut(QKeySequence(QStringLiteral("Ctrl+,")));
  actions_.settings->setToolTip(tr("Settings (Ctrl+,)"));

  actions_.zoom_in = new QAction(QStringLiteral("+"), this);
  actions_.zoom_in->setObjectName(QStringLiteral("zoomInAction"));
  actions_.zoom_in->setIcon(ThemeIcon(QStringLiteral("zoom-in"), initial_dark));
  actions_.zoom_in->setToolTip(tr("Zoom In (Ctrl+=)"));

  actions_.bookmark = new QAction(tr("Bookmark Current Document"), this);
  actions_.bookmark->setObjectName(QStringLiteral("bookmarkAction"));
  actions_.bookmark->setShortcut(QKeySequence(QStringLiteral("Ctrl+D")));
  actions_.bookmark->setToolTip(tr("Bookmark Current Document (Ctrl+D)"));
  UpdateHistoryActions();

  // 汉堡菜单：文件/视图/帮助三组动作
  app_menu_ = new QMenu(this);
  QMenu* file_menu = app_menu_->addMenu(tr("&File"));
  file_menu->addAction(actions_.close_tab);
  file_menu->addAction(actions_.reopen_tab);
  file_menu->addAction(actions_.new_window);
  file_menu->addSeparator();
  file_menu->addAction(actions_.open_file);
  file_menu->addAction(actions_.open_dir);
  file_menu->addAction(actions_.unload_workspace);
  recent_menu_ = file_menu->addMenu(tr("Recent Files"));
  recent_roots_menu_ = file_menu->addMenu(tr("Recent Workspaces"));
  file_menu->addSeparator();
  file_menu->addAction(actions_.export_html);
  file_menu->addAction(actions_.export_pdf);
  file_menu->addSeparator();
  file_menu->addAction(actions_.quit);

  view_menu_ = app_menu_->addMenu(tr("&View"));
  view_menu_->addAction(actions_.quick_open);
  view_menu_->addAction(actions_.workspace_search);
  view_menu_->addSeparator();
  view_menu_->addAction(actions_.view_documents);
  view_menu_->addAction(actions_.view_bookmarks);
  view_menu_->addAction(actions_.view_render_status);
  view_menu_->addAction(actions_.outline);
  view_menu_->addSeparator();
  view_menu_->addAction(actions_.back);
  view_menu_->addAction(actions_.forward);
  actions_.refresh = new QAction(tr("Refresh"), this);
  actions_.refresh->setObjectName(QStringLiteral("refreshAction"));
  actions_.refresh->setShortcut(QKeySequence(Qt::Key_F5));
  view_menu_->addAction(actions_.refresh);
  view_menu_->addSeparator();
  view_menu_->addAction(actions_.zoom_out);
  view_menu_->addAction(actions_.zoom_reset);
  view_menu_->addAction(actions_.zoom_in);
  view_menu_->addSeparator();
  view_menu_->addAction(actions_.settings);
#ifdef Q_OS_WIN
  QMenu* material_menu = view_menu_->addMenu(tr("Window Material"));
  auto* material_group = new QActionGroup(this);
  actions_.material_auto = material_menu->addAction(tr("Auto (Mica)"));
  actions_.material_auto->setObjectName(QStringLiteral("materialAutoAction"));
  actions_.material_acrylic = material_menu->addAction(tr("Acrylic"));
  actions_.material_acrylic->setObjectName(
      QStringLiteral("materialAcrylicAction"));
  actions_.material_none = material_menu->addAction(tr("Close"));
  actions_.material_none->setObjectName(QStringLiteral("materialNoneAction"));
  for (QAction* action : {actions_.material_auto, actions_.material_acrylic,
                          actions_.material_none}) {
    action->setCheckable(true);
    material_group->addAction(action);
  }
  connect(actions_.material_auto, &QAction::triggered, this,
          [this] { ApplyWindowMaterial(WindowMaterial::kAuto, true); });
  connect(actions_.material_acrylic, &QAction::triggered, this,
          [this] { ApplyWindowMaterial(WindowMaterial::kAcrylic, true); });
  connect(actions_.material_none, &QAction::triggered, this,
          [this] { ApplyWindowMaterial(WindowMaterial::kNone, true); });
  UpdateMaterialActions();
#endif

  QMenu* help_menu = app_menu_->addMenu(tr("&Help"));
  help_menu->addAction(actions_.about);

  // 菜单挂到标题栏按钮后不再是 QMenuBar；关键动作仍挂到窗口，快捷键才稳
  addAction(actions_.open_file);
  addAction(actions_.open_dir);
  addAction(actions_.close_tab);
  addAction(actions_.reopen_tab);
  addAction(actions_.new_window);
  addAction(actions_.quit);
  addAction(actions_.find);
  addAction(actions_.workspace_search);
  addAction(actions_.view_documents);
  addAction(actions_.view_bookmarks);
  addAction(actions_.view_render_status);
  addAction(actions_.outline);
  addAction(actions_.quick_open);
  addAction(actions_.back);
  addAction(actions_.forward);
  addAction(actions_.bookmark);
  addAction(actions_.refresh);

  menu_button_ = new QToolButton(title_bar_);
  menu_button_->setObjectName(QStringLiteral("mainMenuButton"));
  menu_button_->setAutoRaise(true);
  const QIcon menu_icon =
      ThemeIcon(QStringLiteral("menu"), settings_->dark_theme);
  if (menu_icon.isNull()) {
    menu_button_->setText(QStringLiteral("≡"));
  } else {
    menu_button_->setIcon(menu_icon);
  }
  menu_button_->setPopupMode(QToolButton::InstantPopup);
  menu_button_->setMenu(app_menu_);
  menu_button_->setToolTip(tr("Menu"));

  // 标题栏左侧：菜单 + 标签条 + 新建；标签条吃掉剩余宽度
  title_leading_widget_ = new QWidget(title_bar_);
  title_leading_widget_->setObjectName(QStringLiteral("titleLeadingWidget"));
  // 背景交给标题栏统一铺，否则半透明 chrome 会多叠一层
  auto* leading_layout = new QHBoxLayout(title_leading_widget_);
  // 标签 38px 高，顶部留 4px；标题栏整体高度见下面的 setFixedHeight
  leading_layout->setContentsMargins(0, 4, 0, 0);
  leading_layout->setSpacing(2);
  leading_layout->addWidget(menu_button_);
  tab_bar_ = new DocumentTabBar(title_leading_widget_);
  tab_bar_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
  leading_layout->addWidget(tab_bar_);
  // 拖拽区保底宽度：标签条吃满 leading 时窗口仍要有可拖动区域
  auto* title_drag = new WindowDragArea(title_leading_widget_);
  title_drag->setObjectName(QStringLiteral("titleDragArea"));
  title_drag->setMinimumWidth(96);
  leading_layout->addWidget(title_drag, 1);
  title_bar_->setLeadingWidget(title_leading_widget_);
  // 标题由标签显示，标题栏不再单独占一行文字；把剩余宽度让给标签条
  title_bar_->setTitleVisible(false);
  if (auto* title_layout = qobject_cast<QHBoxLayout*>(title_bar_->layout())) {
    title_layout->setStretchFactor(title_leading_widget_, 1);
  }
  // 欢迎标签固定第 0 位、不可关闭
  tab_bar_->addTab(QIcon(QStringLiteral(":/resources/svg/icon.svg")),
                   tr("Welcome"));
  tab_bar_->setTabToolTip(0, tr("Welcome Page"));
  tab_bar_->RemoveCloseButton(0);
  // 标签 38 + 顶部 4；标题栏高度跟着标签走，否则会被裁掉
  title_bar_->setFixedHeight(4 + tab_bar_->tabSizeHint(0).height());

  // 标题栏自带的搜索框隐藏；文档条只查当前文档，工作区搜索在侧栏
  title_bar_->searchLineEdit()->setVisible(false);

  // QWK 接管：标题栏是右列里的普通子控件；可交互子控件逐个登记 hit-test，
  // 系统按钮交给 agent 才能对上系统菜单/Snap 等原生行为
  setAttribute(Qt::WA_DontCreateNativeAncestors);
  window_agent_ = new QWK::WidgetWindowAgent(this);
  window_agent_->setup(this);
  window_agent_->setTitleBar(title_bar_);
  window_agent_->setHitTestVisible(title_leading_widget_, true);
  window_agent_->setHitTestVisible(menu_button_, true);
  window_agent_->setHitTestVisible(tab_bar_, true);
  window_agent_->setHitTestVisible(title_bar_->themeButton(), true);
  window_agent_->setHitTestVisible(title_bar_->pinButton(), true);
  window_agent_->setSystemButton(QWK::WindowAgentBase::Minimize,
                                 title_bar_->minButton());
  window_agent_->setSystemButton(QWK::WindowAgentBase::Maximize,
                                 title_bar_->maxButton());
  window_agent_->setSystemButton(QWK::WindowAgentBase::Close,
                                 title_bar_->closeButton());
  document_find_edit_ = document_bar_->find_edit();
  document_find_edit_->installEventFilter(this);
  workspace_search_edit_ = side_panel_->search_edit();
  document_bar_->back_button()->setDefaultAction(actions_.back);
  document_bar_->forward_button()->setDefaultAction(actions_.forward);
  document_bar_->zoom_out_button()->setDefaultAction(actions_.zoom_out);
  document_bar_->zoom_in_button()->setDefaultAction(actions_.zoom_in);
  // 导出按钮的菜单项与 File 菜单共用同一批 QAction：启用态、翻译和
  // 触发逻辑只有一份
  document_bar_->export_menu()->addAction(actions_.export_html);
  document_bar_->export_menu()->addAction(actions_.export_pdf);
  connect(document_bar_->zoom_reset_button(), &QToolButton::clicked, this,
          [this] { actions_.zoom_reset->trigger(); });
  connect(document_bar_->star_button(), &QToolButton::clicked, this,
          &MdViewerWindow::SetBookmarkChecked);
  connect(document_bar_->appearance_button(), &QToolButton::clicked, this,
          &MdViewerWindow::ShowReaderAppearance);
  connect(welcome_bar_, &WelcomeBar::quickOpenRequested, this, [this] {
    if (actions_.quick_open != nullptr) {
      actions_.quick_open->trigger();
    }
  });
  if (activity_bar_ != nullptr &&
      activity_bar_->sidebar_toggle_button() != nullptr) {
    connect(activity_bar_->sidebar_toggle_button(), &QToolButton::clicked, this,
            [this] { ApplySidebarVisible(!layout_.sidebar_visible); });
  }
  // triggered 只在用户激活时发；setChecked 不会触发它，避免同步状态绕回
  connect(actions_.toggle_sidebar, &QAction::triggered, this,
          [this] { ApplySidebarVisible(!layout_.sidebar_visible); });
  connect(activity_bar_, &ActivityBar::settingsRequested, this,
          &MdViewerWindow::ShowSettings);
  connect(activity_bar_, &ActivityBar::viewActivated, this,
          &MdViewerWindow::ShowSideView);
  connect(shell_splitter_, &QSplitter::splitterMoved, this, [this] {
    if (side_panel_ != nullptr && side_panel_->isVisible()) {
      layout_.sidebar_width = side_panel_->width();
    }
  });
  connect(main_splitter_, &QSplitter::splitterMoved, this, [this] {
    if (outline_column_ != nullptr && outline_column_->isVisible()) {
      layout_.outline_width = outline_column_->width();
    }
  });
  connect(outline_hide_button_, &QToolButton::clicked, this, [this] {
    layout_.outline_visible = false;
    if (actions_.outline != nullptr) {
      actions_.outline->setChecked(false);
    }
    UpdateOutlineVisibility();
  });
  connect(actions_.view_documents, &QAction::triggered, this,
          [this] { EnsureSideView(SideView::kDocuments); });
  connect(actions_.view_bookmarks, &QAction::triggered, this,
          [this] { EnsureSideView(SideView::kBookmarks); });
  connect(actions_.view_render_status, &QAction::triggered, this,
          [this] { EnsureSideView(SideView::kRenderStatus); });
  connect(actions_.outline, &QAction::toggled, this, [this](bool on) {
    layout_.outline_visible = on;
    UpdateOutlineVisibility();
  });
  // 大纲开关的快捷入口：点击翻转现有动作，选中态与视图菜单、
  // 大纲标题栏的 × 按钮保持一致
  document_bar_->outline_button()->setCheckable(true);
  connect(document_bar_->outline_button(), &QToolButton::clicked,
          actions_.outline, &QAction::toggle);
  connect(actions_.outline, &QAction::toggled, document_bar_->outline_button(),
          &QToolButton::setChecked);
  document_bar_->outline_button()->setChecked(actions_.outline->isChecked());
  connect(document_bar_->copy_button(), &QToolButton::clicked, this, [this] {
    const QString path =
        CurrentTab() != nullptr ? CurrentTab()->path : QString();
    if (path.isEmpty()) {
      return;
    }
    QGuiApplication::clipboard()->setText(QDir::toNativeSeparators(path));
    statusBar()->showMessage(tr("Path copied"), 2000);
  });
  search_controller_ = new SearchController(this);
  root_watcher_ = new QFileSystemWatcher(this);
  root_rescan_timer_ = new QTimer(this);
  root_rescan_timer_->setSingleShot(true);
  root_rescan_timer_->setInterval(300);

  RefreshRecentRootsMenu();
  ApplyWindowMaterial(settings_->window_material, /*announce=*/false);
  ApplyReadingStyle();
  UpdateOutlineVisibility();
  UpdateBookmarkUi();
  RefreshRecentMenu();
}

void MdViewerWindow::initSignals() {
  // 标题栏自带的主题按钮：Gallery 同款，点了就整窗（壳 + 文档）换肤
  connect(title_bar_->themeButton(), &QToolButton::clicked, this,
          &MdViewerWindow::ToggleTheme);
  connect(title_bar_->pinButton(), &QToolButton::toggled, this,
          [this](bool checked) { SetTopMost(this, checked); });
  connect(settings_panel_, &SettingsPanel::themeChanged, this,
          &MdViewerWindow::ApplyTheme);
  connect(settings_panel_, &SettingsPanel::windowMaterialChanged, this,
          [this](WindowMaterial material) {
            ApplyWindowMaterial(material, /*announce=*/true);
          });
  connect(settings_panel_, &SettingsPanel::readingStyleChanged, this,
          [this](ReadingStyle style) {
            settings_->reading_style = style;
            session_controller_->SaveAll();
            ApplyReadingStyle();
            statusBar()->showMessage(style == ReadingStyle::kPrototype
                                         ? tr("Reading style: prototype")
                                         : tr("Reading style: library default"),
                                     2000);
          });
  connect(settings_panel_, &SettingsPanel::transparentReadingChanged, this,
          [this](bool enabled) {
            settings_->transparent_reading = enabled;
            session_controller_->SaveAll();
            UpdateReadingSurface();
            statusBar()->showMessage(enabled
                                         ? tr("Reading area: transparent")
                                         : tr("Reading area: theme background"),
                                     2000);
          });
  connect(actions_.back, &QAction::triggered, this, &MdViewerWindow::GoBack);
  connect(actions_.forward, &QAction::triggered, this,
          &MdViewerWindow::GoForward);
  connect(actions_.zoom_in, &QAction::triggered, this, [this] {
    if (CurrentView() != nullptr) {
      CurrentView()->ZoomIn();
    }
  });
  connect(actions_.zoom_out, &QAction::triggered, this, [this] {
    if (CurrentView() != nullptr) {
      CurrentView()->ZoomOut();
    }
  });
  connect(actions_.zoom_reset, &QAction::triggered, this, [this] {
    if (CurrentView() != nullptr) {
      CurrentView()->ResetZoom();
    }
  });
  connect(actions_.open_file, &QAction::triggered, this,
          &MdViewerWindow::OpenFileDialog);
  connect(actions_.open_dir, &QAction::triggered, this,
          &MdViewerWindow::OpenDirectoryDialog);
  connect(actions_.unload_workspace, &QAction::triggered, this,
          &MdViewerWindow::UnloadWorkspace);
  connect(actions_.quit, &QAction::triggered, this, &QWidget::close);
  connect(actions_.about, &QAction::triggered, this, [this] {
    QMessageBox::about(
        this, tr("About"),
        tr("qt_md_viewer — the Markdown viewer app of qlitemd.\n\n"
           "UI font: MiSans (Xiaomi MiSans font license)\n\n"
           "Author: zhouyonghua\n"
           "Email: zhouyohu@163.com"));
  });
  connect(welcome_, &WelcomeView::openFileRequested, this,
          &MdViewerWindow::OpenFileDialog);
  connect(welcome_, &WelcomeView::openDirectoryRequested, this,
          &MdViewerWindow::OpenDirectoryDialog);
  connect(welcome_, &WelcomeView::quickOpenRequested, this,
          &MdViewerWindow::ShowQuickOpen);
  connect(welcome_, &WelcomeView::recentActivated, this,
          [this](const QString& path) {
            NavigateTo(path, QString(), /*record_history=*/true);
          });
  // 点树里的文件换文档；树在根外打开书签时不换根
  connect(side_panel_, &SidePanel::fileActivated, this,
          [this](const QString& path, bool new_tab) {
            NavigateTo(path, QString(), /*record_history=*/true,
                       new_tab ? OpenIntent::kNewTab : OpenIntent::kReplace);
          });
  connect(side_panel_, &SidePanel::chooseRootRequested, this,
          &MdViewerWindow::OpenDirectoryDialog);
  connect(side_panel_, &SidePanel::bookmarkActivated, this,
          &MdViewerWindow::OpenBookmark);
  connect(side_panel_, &SidePanel::bookmarkToggleRequested, this,
          &MdViewerWindow::ToggleBookmarkForPath);
  connect(side_panel_, &SidePanel::bookmarkRemoveRequested, this,
          [this](const QString& path) {
            bookmark_controller_->Remove(path);
            bookmark_controller_->Save();
            UpdateBookmarkUi();
          });
  connect(actions_.bookmark, &QAction::triggered, this, [this] {
    const QString path = CurrentTab()->path;
    SetBookmarkChecked(!path.isEmpty() &&
                       !bookmark_controller_->Contains(path));
  });
  connect(outline_, &MarkdownOutlineWidget::headingActivated, this,
          [this](const QString& anchor) {
            NavigateTo(CurrentView()->current_file(), anchor,
                       /*record_history=*/true);
          });
  connect(settings_panel_, &SettingsPanel::statusMessage, this,
          [this](const QString& message) {
            statusBar()->showMessage(message, 5000);
          });
  connect(settings_panel_, &SettingsPanel::settingsChanged, this, [this] {
    CaptureGlobalSettings(CurrentTab());
    if (CurrentView() == nullptr) {
      return;
    }
    // 面板只写了活动视图；把设置播到其它标签，缓存目录与诊断
    // 不在 settings_ 里，按活动视图复制
    const QString cache_dir =
        CurrentView()->render_context().options().cache_directory;
    const MarkdownDiagnostics diagnostics = CurrentView()->diagnostics();
    for (const auto& tab : tabs_) {
      if (tab->view == nullptr || tab->view == CurrentView()) {
        continue;
      }
      tab->view->SetRenderCacheDirectory(cache_dir);
      tab->view->SetDiagnostics(diagnostics);
      ApplyGlobalSettings(tab.get());
    }
    SaveTabSession();
  });
  connect(actions_.settings, &QAction::triggered, this,
          &MdViewerWindow::ShowSettings);
  connect(render_status_, &RenderStatusPanel::statusMessage, this,
          [this](const QString& message) {
            statusBar()->showMessage(message, 5000);
          });
  connect(actions_.export_html, &QAction::triggered, this,
          &MdViewerWindow::ExportHtml);
  connect(actions_.export_pdf, &QAction::triggered, this,
          &MdViewerWindow::ExportPdf);
  connect(actions_.quick_open, &QAction::triggered, this,
          &MdViewerWindow::ShowQuickOpen);
  connect(actions_.find, &QAction::triggered, this,
          &MdViewerWindow::FocusDocumentFind);
  connect(actions_.workspace_search, &QAction::triggered, this,
          &MdViewerWindow::FocusWorkspaceSearch);
  // 当前文档查找：输入即找，回车下一个，Shift+回车上一个
  connect(document_find_edit_, &QLineEdit::textChanged, this,
          [this](const QString& text) {
            ClearFindCount();
            if (text.isEmpty()) {
              search_controller_->StopDocumentFind();
              if (CurrentView() != nullptr) {
                CurrentView()->ClearSelection();
              }
              return;
            }
            if (document_find_edit_->isEnabled()) {
              search_controller_->ScheduleDocumentFind(text);
            } else {
              search_controller_->StopDocumentFind();
            }
          });
  connect(search_controller_, &SearchController::documentFindRequested, this,
          [this](const QString& query, bool backward) {
            FindInDocument(query, backward);
          });
  connect(document_find_edit_, &QLineEdit::returnPressed, this, [this] {
    search_controller_->FindDocumentNow(document_find_edit_->text(), false);
  });
  connect(document_bar_, &DocumentBar::FindPreviousRequested, this, [this] {
    if (document_find_edit_->isEnabled()) {
      search_controller_->FindDocumentNow(document_find_edit_->text(), true);
    }
  });
  connect(document_bar_, &DocumentBar::FindNextRequested, this, [this] {
    if (document_find_edit_->isEnabled()) {
      search_controller_->FindDocumentNow(document_find_edit_->text(), false);
    }
  });
  // 工作区搜索：输入即重置并防抖，回车跳过防抖
  connect(side_panel_, &SidePanel::searchTextChanged, this,
          [this](const QString& text) {
            search_controller_->ScheduleWorkspaceSearch(root_dir_, text);
          });
  connect(search_controller_, &SearchController::workspaceSearchReset,
          side_panel_, &SidePanel::BeginSearch);
  connect(search_controller_, &SearchController::workspaceSearchBatch,
          side_panel_, &SidePanel::AppendSearchHits);
  connect(search_controller_, &SearchController::workspaceSearchFinished,
          side_panel_, &SidePanel::EndSearch);
  connect(search_controller_, &SearchController::workspaceSearchFailed,
          side_panel_, &SidePanel::SetSearchError);
  connect(side_panel_, &SidePanel::searchSubmitted, this, [this] {
    search_controller_->SearchWorkspaceNow(root_dir_,
                                           workspace_search_edit_->text());
  });
  connect(side_panel_, &SidePanel::searchHitActivated, this,
          &MdViewerWindow::OpenSearchHit);
  connect(side_panel_, &SidePanel::cancelSearchRequested, this,
          [this] { search_controller_->CancelWorkspaceSearch(); });
  connect(root_watcher_, &QFileSystemWatcher::directoryChanged, this,
          [this](const QString&) { root_rescan_timer_->start(); });
  connect(root_watcher_, &QFileSystemWatcher::fileChanged, this,
          [this](const QString&) { root_rescan_timer_->start(); });
  connect(root_rescan_timer_, &QTimer::timeout, this,
          &MdViewerWindow::RefreshRoot);
  connect(actions_.refresh, &QAction::triggered, this,
          &MdViewerWindow::RefreshRoot);
  // Esc 把焦点还给正文；文档查找框的 Esc 在 eventFilter 里处理
  auto* escape = new QShortcut(QKeySequence(Qt::Key_Escape), this);
  connect(escape, &QShortcut::activated, this, [this] {
    if (workspace_search_edit_ != nullptr &&
        workspace_search_edit_->hasFocus() && CurrentView() != nullptr) {
      CurrentView()->setFocus();
    }
  });
  connect(actions_.new_window, &QAction::triggered, this, [this] {
    // 次窗口不读写会话：停在自己的欢迎页，多工作区靠它实现
    auto* window = new MdViewerWindow(settings_, /*session_owner=*/false);
    window->setAttribute(Qt::WA_DeleteOnClose);
    window->resize(1100, 800);
    window->show();
  });
  connect(actions_.close_tab, &QAction::triggered, this,
          [this] { CloseTab(active_tab_); });
  connect(actions_.reopen_tab, &QAction::triggered, this,
          &MdViewerWindow::ReopenClosedTab);
  if (tab_bar_ != nullptr) {
    connect(tab_bar_, &QTabBar::currentChanged, this, [this](int) {
      // QTabBar::moveTab 可能先改 currentIndex 再发 tabMoved；延后一拍，
      // 等模型顺序稳定后再按最终下标激活
      if (tab_switch_queued_) {
        return;
      }
      tab_switch_queued_ = true;
      QTimer::singleShot(0, this, [this] {
        tab_switch_queued_ = false;
        if (tab_bar_ != nullptr && tab_bar_->currentIndex() >= 0) {
          ActivateTab(tab_bar_->currentIndex());
        }
      });
    });
    connect(tab_bar_, &QTabBar::tabCloseRequested, this,
            &MdViewerWindow::CloseTab);
    connect(tab_bar_, &DocumentTabBar::closeTabRequested, this,
            &MdViewerWindow::CloseTab);
    connect(tab_bar_, &QTabBar::tabMoved, this, &MdViewerWindow::OnTabMoved);
    connect(tab_bar_, &DocumentTabBar::reloadTabRequested, this,
            &MdViewerWindow::ReloadTab);
    connect(tab_bar_, &DocumentTabBar::reopenClosedTabRequested, this,
            &MdViewerWindow::ReopenClosedTab);
  }
  // P2 的标签条到位前，先用键盘在标签间切换
  auto* next_tab =
      new QShortcut(QKeySequence(QStringLiteral("Ctrl+Tab")), this);
  connect(next_tab, &QShortcut::activated, this, [this] {
    if (tabs_.size() > 1) {
      ActivateTab((active_tab_ + 1) % static_cast<int>(tabs_.size()));
    }
  });
  auto* previous_tab =
      new QShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+Tab")), this);
  connect(previous_tab, &QShortcut::activated, this, [this] {
    if (tabs_.size() > 1) {
      ActivateTab((active_tab_ + static_cast<int>(tabs_.size()) - 1) %
                  static_cast<int>(tabs_.size()));
    }
  });
}

void MdViewerWindow::OpenPath(const QString& path) {
  NavigateTo(path, QString(), /*record_history=*/true, OpenIntent::kNewTab);
}

void MdViewerWindow::NavigateTo(const QString& path, const QString& fragment,
                                bool record_history, OpenIntent intent) {
  // 正在渲染时旧 HTML 还在屏幕上：同目标只更新待滚锚点，别再滚旧文档
  if (CurrentView()->is_rendering() && path == CurrentView()->pending_file()) {
    CurrentTab()->pending_fragment = fragment;
    if (record_history) {
      PushHistory(path, fragment);
    }
    return;
  }
  // 同文件只滚锚点，不重加载；否则后退到同文档的另一个锚点会闪一下
  if (!CurrentView()->current_file().isEmpty() &&
      path == CurrentView()->current_file()) {
    if (CurrentView()->is_rendering()) {
      // 用户回到了屏幕上已提交的文档：放弃还没提交的换篇
      CurrentView()->CancelPendingRender();
      CurrentTab()->pending_fragment.clear();
      render_progress_->hide();
      CurrentTab()->render_request = 0;
      SetTabLoading(CurrentTab(), false);
      CurrentTab()->path = CurrentView()->current_file();
      UpdateBookmarkUi();
    }
    if (record_history) {
      PushHistory(path, fragment);
    }
    ScrollToFragment(fragment);
    return;
  }
  // 新文档的锚点要等提交后再滚；旧文档的 heading_ids_ 里没有它
  DocumentTab* source_tab = CurrentTab();
  const int existing_before = FindTabIndex(path);
  const bool switches_to_open_tab = intent != OpenIntent::kHistory &&
                                    existing_before > 0 &&
                                    existing_before != active_tab_;
  source_tab->pending_fragment = fragment;
  const QString opened = OpenPathCore(path, intent);
  if (opened.isEmpty()) {
    CurrentTab()->pending_fragment.clear();
    return;
  }
  if (record_history) {
    PushHistory(opened, fragment);
  }
  if (switches_to_open_tab && CurrentTab() != source_tab) {
    // 待滚锚点原本记在旧标签上；换到已打开标签后归位，且不重置滚动
    source_tab->pending_fragment.clear();
    if (!fragment.isEmpty()) {
      CurrentTab()->pending_fragment = fragment;
      if (!CurrentView()->is_rendering()) {
        CurrentTab()->pending_fragment.clear();
        ScrollToFragment(fragment);
      }
    }
  } else if (fragment.isEmpty()) {
    CurrentTab()->pending_fragment.clear();
    ScrollToFragment(QString());
  }
}

DocumentScanResult MdViewerWindow::SetRootDir(const QString& root) {
  // 换根后旧根的结果没有意义，先取消扫描并清空结果页
  if (search_controller_ != nullptr) {
    search_controller_->ResetWorkspaceSearch();
    side_panel_->BeginSearch(QString());
  }
  const DocumentScanResult result = ScanDocumentRoot(root);
  if (result.root_exists) {
    root_dir_ = result.root;
    // 任何一次成功换根都视为"重新有了工作区"，解除卸载标志
    session_controller_->SetRoot(root_dir_);
    settings_->AddRecentRoot(root_dir_);
    RefreshRecentRootsMenu();
    SaveTabSession();
    if (actions_.unload_workspace != nullptr) {
      actions_.unload_workspace->setEnabled(true);
    }
    side_panel_->SetRoot(result);
    side_panel_->SetSearchAvailable(true);
    quick_open_files_.clear();
    quick_open_files_.reserve(result.markdown_file_count);
    for (const DocumentEntry& entry : result.entries) {
      if (!entry.is_dir) {
        quick_open_files_.append(entry.absolute_path);
      }
    }
  } else {
    quick_open_files_.clear();
    root_dir_ = QFileInfo(root).absoluteFilePath();
    side_panel_->SetRootMissing(root_dir_,
                                tr("Folder does not exist or is not readable"));
    side_panel_->SetSearchAvailable(false);
  }
  if (document_count_label_ != nullptr) {
    document_count_label_->setText(
        tr("%1 documents")
            .arg(result.root_exists ? result.markdown_file_count : 0));
  }
  if (root_watcher_ != nullptr) {
    if (!watched_dirs_.isEmpty()) {
      root_watcher_->removePaths(watched_dirs_);
    }
    watched_dirs_.clear();
    if (result.root_exists) {
      watched_dirs_ = DirectoriesToWatch(result);
      if (!watched_dirs_.isEmpty()) {
        root_watcher_->addPaths(watched_dirs_);
      }
    }
  }
  UpdateBookmarkUi();
  return result;
}

void MdViewerWindow::RefreshRoot() {
  if (root_dir_.isEmpty()) {
    return;
  }
  if (root_rescan_timer_ != nullptr) {
    root_rescan_timer_->stop();
  }
  SetRootDir(root_dir_);
}

QString MdViewerWindow::OpenPathCore(const QString& path, OpenIntent intent) {
  const QFileInfo info(path);
  if (info.isDir()) {
    const DocumentScanResult result = SetRootDir(info.absoluteFilePath());
    if (!result.root_exists || result.markdown_file_count == 0) {
      statusBar()->showMessage(
          tr("No Markdown files in folder: %1").arg(result.root), 5000);
      return {};
    }
    for (const DocumentEntry& entry : result.entries) {
      if (!entry.is_dir) {
        return ShowFile(entry.absolute_path, OpenReason::kUser, intent)
                   ? entry.absolute_path
                   : QString();
      }
    }
    return {};
  }
  const QString absolute = info.absoluteFilePath();
  // 根外文件直接在标签里打开，不重建根；只有“打开目录”才改根
  return ShowFile(absolute, OpenReason::kUser, intent) ? absolute : QString();
}

void MdViewerWindow::PushHistory(const QString& path, const QString& fragment) {
  CurrentTab()->history.Push({path, fragment});
  UpdateHistoryActions();
}

void MdViewerWindow::ScrollToFragment(const QString& fragment) {
  if (fragment.isEmpty()) {
    CurrentView()->SetScrollPosition(0);
    return;
  }
  CurrentView()->ScrollToAnchor(fragment);
}

void MdViewerWindow::GoBack() {
  const std::optional<NavigationEntry> entry = CurrentTab()->history.Back();
  if (!entry.has_value()) {
    return;
  }
  NavigateTo(entry->path, entry->fragment, /*record_history=*/false,
             OpenIntent::kHistory);
  UpdateHistoryActions();
}

void MdViewerWindow::GoForward() {
  const std::optional<NavigationEntry> entry = CurrentTab()->history.Forward();
  if (!entry.has_value()) {
    return;
  }
  NavigateTo(entry->path, entry->fragment, /*record_history=*/false,
             OpenIntent::kHistory);
  UpdateHistoryActions();
}

void MdViewerWindow::UpdateHistoryActions() {
  if (actions_.back != nullptr) {
    actions_.back->setEnabled(CurrentTab()->history.CanGoBack());
  }
  if (actions_.forward != nullptr) {
    actions_.forward->setEnabled(CurrentTab()->history.CanGoForward());
  }
}

void MdViewerWindow::RememberCurrentPosition() {
  // 占位页的 ScrollPosition 恒为 0；渲染中记录会把上一篇的位置冲掉
  if (CurrentView() == nullptr || !CurrentView()->has_document() ||
      CurrentView()->is_rendering()) {
    return;
  }
  const QString path = CurrentView()->current_file();
  if (path.isEmpty()) {
    return;
  }
  position_controller_->Remember(path, CurrentView()->ScrollPosition(),
                                 QDateTime::currentSecsSinceEpoch());
}

void MdViewerWindow::RestoreSession() {
  // 上次显式卸载了工作区：重启回欢迎页，最近列表照常显示，
  // 但不自动恢复任何文档
  if (!session_controller_->RestoreRequested()) {
    side_panel_->SetRootMissing(QString(),
                                tr("Start with \"File > Open Folder\""));
    ShowWelcome();
    return;
  }
  // 恢复期间不改会话：SetRootDir/ShowFile 内部的保存都跳过，
  // 恢复完统一写回一次
  int saved_active = 0;
  const QVector<QString> saved_paths =
      session_controller_->SavedTabPaths(&saved_active);
  session_controller_->BeginRestore();
  struct RestoreScope {
    SessionController* controller;
    ~RestoreScope() { controller->EndRestore(); }
  } restore_scope{session_controller_.get()};
  const QString saved_root = session_controller_->SavedRoot();
  if (!saved_root.isEmpty()) {
    const DocumentScanResult result = SetRootDir(saved_root);
    if (!result.root_exists) {
      ShowWelcome();
      return;  // SetRootDir 已显示空态
    }
  }
  // 多标签会话优先；缺失的文件跳过，不阻塞其它标签
  if (!saved_paths.isEmpty()) {
    for (int i = 1; i < saved_paths.size(); ++i) {
      const QString path = saved_paths.at(i);
      if (!QFileInfo(path).isFile()) {
        continue;
      }
      if (ShowFile(path, OpenReason::kRestore, OpenIntent::kNewTab)) {
        PushHistory(path, QString());
      }
    }
    if (tabs_.size() > 1) {
      const QString active_path = saved_paths.value(saved_active);
      const int index = active_path.isEmpty() ? 0 : FindTabIndex(active_path);
      if (index >= 0) {
        ActivateTab(index);
      }
      WriteTabSession();
      return;
    }
  }
  // 旧会话（没有 open_tabs）按 last_file 恢复
  if (!saved_root.isEmpty()) {
    const QString saved_last_file = session_controller_->SavedLastFile();
    if (QFileInfo(saved_last_file).isFile()) {
      const QString absolute = QFileInfo(saved_last_file).absoluteFilePath();
      if (ShowFile(absolute, OpenReason::kRestore, OpenIntent::kNewTab)) {
        PushHistory(absolute, QString());
      }
    }
    if (!CurrentView()->is_rendering() && !CurrentView()->has_document()) {
      ShowWelcome();
    }
    WriteTabSession();
    return;
  }
  OpenLastFile();
  WriteTabSession();
}

void MdViewerWindow::OpenLastFile() {
  for (const QString& path : settings_->recent_files) {
    if (!QFileInfo(path).isFile()) {
      continue;
    }
    const QString absolute = QFileInfo(path).absoluteFilePath();
    if (ShowFile(absolute, OpenReason::kRestore, OpenIntent::kNewTab)) {
      PushHistory(absolute, QString());
      statusBar()->showMessage(
          tr("Restored last opened: %1").arg(QFileInfo(path).fileName()), 3000);
    }
    return;
  }
  side_panel_->SetRootMissing(QString(),
                              tr("Start with \"File > Open Folder\""));
  ShowWelcome();
}

void MdViewerWindow::OpenFileDialog() {
  const QString start_dir = root_dir_.isEmpty() ? QDir::homePath() : root_dir_;
  const QString path = QFileDialog::getOpenFileName(
      this, tr("Open Markdown"), start_dir,
      tr("Markdown Documents (*.md *.markdown);;All Files (*)"));
  if (!path.isEmpty()) {
    OpenPath(path);
  }
}

void MdViewerWindow::OpenDirectoryDialog() {
  const QString dir = QFileDialog::getExistingDirectory(
      this, tr("Open Folder"),
      root_dir_.isEmpty() ? QDir::homePath() : root_dir_);
  if (!dir.isEmpty()) {
    OpenPath(dir);
  }
}

void MdViewerWindow::UnloadWorkspace() {
  if (root_dir_.isEmpty()) {
    return;
  }
  const QString unloaded_root = root_dir_;
  // 当前文档的阅读位置先落盘，之后从最近打开还能接着读
  RememberCurrentPosition();
  if (search_controller_ != nullptr) {
    search_controller_->ResetWorkspaceSearch();
  }
  side_panel_->BeginSearch(QString());
  side_panel_->SetCurrentFile(QString());
  side_panel_->SetRootMissing(QString(),
                              tr("Start with \"File > Open Folder\""));
  side_panel_->SetSearchAvailable(false);

  root_dir_.clear();
  // 意图标志：没有它，重启时 OpenLastFile 会从最近列表把工作区拉回来
  session_controller_->ClearWorkspace();

  quick_open_files_.clear();
  if (root_watcher_ != nullptr && !watched_dirs_.isEmpty()) {
    root_watcher_->removePaths(watched_dirs_);
  }
  watched_dirs_.clear();
  // 历史里的路径属于旧工作区；留着会让“后退”把它静默拉回来
  ClearDocumentTabs();
  tabs_[0]->history.Clear();
  UpdateHistoryActions();

  tabs_[0]->pending_fragment.clear();
  tabs_[0]->path.clear();
  tabs_[0]->view->ClearDocument();
  setWindowTitle(QStringLiteral("qlitemd"));
  if (document_bar_ != nullptr) {
    document_bar_->SetPath(QString(), QString());
  }
  document_count_label_->setText(tr("0 documents"));
  UpdateBookmarkUi();
  actions_.unload_workspace->setEnabled(false);
  statusBar()->showMessage(
      tr("Workspace unloaded: %1").arg(QFileInfo(unloaded_root).fileName()),
      4000);
  ShowWelcome();
}

void MdViewerWindow::ShowQuickOpen() {
  if (quick_open_ == nullptr) {
    quick_open_ = new QuickOpenPopup(this);
    connect(quick_open_, &QuickOpenPopup::itemActivated, this,
            &MdViewerWindow::ActivateQuickOpenItem);
  }

  QVector<QuickOpenItem> items;
  const QString current_file = CurrentView()->current_file();
  if (CurrentView()->has_document() && !CurrentView()->is_rendering() &&
      !current_file.isEmpty()) {
    int heading_count = 0;
    for (const MarkdownHeading& heading : CurrentView()->outline()) {
      if (heading_count >= kMaxQuickOpenHeadings) {
        break;
      }
      QuickOpenItem item;
      item.kind = QuickOpenKind::kHeading;
      item.title = heading.text;
      item.subtitle = QStringLiteral("%1 · H%2")
                          .arg(QFileInfo(current_file).fileName())
                          .arg(heading.level);
      item.path = current_file;
      item.anchor = heading.anchor;
      items.append(item);
      ++heading_count;
    }
  }

  QSet<QString> seen;
  for (const QString& path : settings_->recent_files) {
    const QFileInfo info(path);
    if (!info.isFile()) {
      continue;
    }
    const QString absolute = info.absoluteFilePath();
    if (seen.contains(absolute)) {
      continue;
    }
    seen.insert(absolute);
    QuickOpenItem item;
    item.kind = QuickOpenKind::kRecent;
    item.title = info.fileName();
    item.subtitle =
        tr("Recent · %1").arg(QDir::toNativeSeparators(info.absolutePath()));
    item.path = absolute;
    items.append(item);
  }

  auto add_command = [&items](const QString& title, const QString& subtitle,
                              const QString& id) {
    QuickOpenItem item;
    item.kind = QuickOpenKind::kCommand;
    item.title = title;
    item.subtitle = subtitle;
    item.command_id = id;
    items.append(item);
  };
  add_command(tr("Open File…"), tr("File"), QStringLiteral("open-file"));
  add_command(tr("Open Folder…"), tr("File"), QStringLiteral("open-dir"));
  if (!root_dir_.isEmpty()) {
    add_command(tr("Unload Workspace"), tr("File"),
                QStringLiteral("unload-workspace"));
  }
  add_command(CurrentView()->theme() == MarkdownTheme::Dark
                  ? tr("Switch to light theme")
                  : tr("Switch to dark theme"),
              tr("Appearance"), QStringLiteral("toggle-theme"));
  add_command(tr("Reading Appearance…"), tr("Appearance"),
              QStringLiteral("appearance"));
  const bool starred = !CurrentTab()->path.isEmpty() &&
                       bookmark_controller_->Contains(CurrentTab()->path);
  add_command(starred ? tr("Unbookmark Current Document")
                      : tr("Bookmark Current Document"),
              tr("Reading"), QStringLiteral("toggle-bookmark"));
  add_command(tr("Export HTML…"), tr("Export"), QStringLiteral("export-html"));
  add_command(tr("Export PDF…"), tr("Export"), QStringLiteral("export-pdf"));
  add_command(tr("Refresh document tree"), tr("View"),
              QStringLiteral("refresh-root"));
  if (CurrentView()->has_document() && !CurrentView()->is_rendering() &&
      !CurrentView()->GeneratedImageUrls().isEmpty()) {
    add_command(tr("Preview formula/diagram"), tr("Reading"),
                QStringLiteral("preview-image"));
  }
  add_command(tr("Zoom Out"), tr("Zoom"), QStringLiteral("zoom-out"));
  add_command(tr("Reset Zoom"), tr("Zoom"), QStringLiteral("zoom-reset"));
  add_command(tr("Zoom In"), tr("Zoom"), QStringLiteral("zoom-in"));
  add_command(tr("Open render settings"), tr("View"),
              QStringLiteral("show-settings"));

  for (const QString& path : quick_open_files_) {
    if (seen.contains(path)) {
      continue;
    }
    seen.insert(path);
    const QFileInfo info(path);
    const QString subtitle = IsPathUnderRoot(path, root_dir_)
                                 ? QDir(root_dir_).relativeFilePath(path)
                                 : info.absolutePath();
    QuickOpenItem item;
    item.kind = QuickOpenKind::kFile;
    item.title = info.fileName();
    item.subtitle = QDir::toNativeSeparators(subtitle);
    item.path = path;
    items.append(item);
  }
  quick_open_->Open(items);
}

void MdViewerWindow::ActivateQuickOpenItem(const QuickOpenItem& item) {
  switch (item.kind) {
    case QuickOpenKind::kHeading:
      if (!item.path.isEmpty() && !item.anchor.isEmpty()) {
        NavigateTo(item.path, item.anchor, /*record_history=*/true);
      }
      return;
    case QuickOpenKind::kFile:
    case QuickOpenKind::kRecent:
      if (!item.path.isEmpty()) {
        NavigateTo(item.path, QString(), /*record_history=*/true);
      }
      return;
    case QuickOpenKind::kCommand:
      break;
  }

  const QString& id = item.command_id;
  if (id == QLatin1String("open-file")) {
    actions_.open_file->trigger();
  } else if (id == QLatin1String("open-dir")) {
    actions_.open_dir->trigger();
  } else if (id == QLatin1String("unload-workspace")) {
    actions_.unload_workspace->trigger();
  } else if (id == QLatin1String("toggle-theme")) {
    ToggleTheme();
  } else if (id == QLatin1String("appearance")) {
    ShowReaderAppearance();
  } else if (id == QLatin1String("preview-image")) {
    const QVector<QUrl> images = CurrentView()->GeneratedImageUrls();
    if (!images.isEmpty()) {
      ShowImagePreview(images.first());
    }
  } else if (id == QLatin1String("toggle-bookmark")) {
    actions_.bookmark->trigger();
  } else if (id == QLatin1String("export-html")) {
    actions_.export_html->trigger();
  } else if (id == QLatin1String("export-pdf")) {
    actions_.export_pdf->trigger();
  } else if (id == QLatin1String("refresh-root")) {
    RefreshRoot();
  } else if (id == QLatin1String("zoom-out")) {
    actions_.zoom_out->trigger();
  } else if (id == QLatin1String("zoom-reset")) {
    actions_.zoom_reset->trigger();
  } else if (id == QLatin1String("zoom-in")) {
    actions_.zoom_in->trigger();
  } else if (id == QLatin1String("show-settings")) {
    ShowSettings();
  }
}

void MdViewerWindow::ApplyTheme(MarkdownTheme theme) {
  const bool dark = theme == MarkdownTheme::Dark;
  qApp->setProperty("_q_colorscheme", dark ? 1 : 0);
  // 重建样式，让 FluentUI3Style 按新的配色方案重算调色板（Gallery 同款）。
  // 只换 style 不会刷新已有控件的 app palette，状态栏/大纲/正文留白会
  // 停在旧主题；显式把新 style 的 standardPalette 推给 qApp。
  qApp->setStyle(new FluentUI3Style);
  qApp->setPalette(qApp->style()->standardPalette());
  title_bar_->setThemeDark(dark);
  if (settings_ != nullptr) {
    settings_->dark_theme = dark;
  }
  MarkdownView* active = CurrentView();
  const bool theme_changed = active != nullptr && active->theme() != theme;
  for (const auto& tab : tabs_) {
    if (tab->view != nullptr) {
      tab->view->SetTheme(theme);
    }
  }
  if (theme_changed && active != nullptr &&
      (active->has_document() || active->is_rendering())) {
    // 换肤同样要等整篇重渲染；否则旧配色会一直留在屏幕上
    active->ShowPlaceholder(tr("Rendering…"));
  }
  // 面板是被动同步方，用 QSignalBlocker 防止再发一次 themeChanged
  settings_panel_->SetTheme(theme);
  RefreshChromePalette();
  UpdateReadingSurface();
  // 星标图标按调色板绘制，换肤后要重画
  RefreshThemeIcons();
  statusBar()->showMessage(theme == MarkdownTheme::Dark
                               ? tr("Theme switched: dark")
                               : tr("Theme switched: light"),
                           3000);
}

void MdViewerWindow::RefreshThemeIcons() {
  // 主题相关图标分 svg/light 与 svg/dark 两套，换肤时整体换一套；
  // 星标里实心星是语义图标，UpdateBookmarkUi 内部处理
  const bool dark = ThemeDark();
  actions_.quick_open->setIcon(ThemeIcon(QStringLiteral("search"), dark));
  actions_.back->setIcon(ThemeIcon(QStringLiteral("back"), dark));
  actions_.forward->setIcon(ThemeIcon(QStringLiteral("forward"), dark));
  actions_.zoom_out->setIcon(ThemeIcon(QStringLiteral("zoom-out"), dark));
  actions_.zoom_reset->setIcon(ThemeIcon(QStringLiteral("zoom-reset"), dark));
  actions_.zoom_in->setIcon(ThemeIcon(QStringLiteral("zoom-in"), dark));
  if (menu_button_ != nullptr) {
    menu_button_->setIcon(ThemeIcon(QStringLiteral("menu"), dark));
  }
  if (document_bar_ != nullptr) {
    document_bar_->SetThemeDark(dark);
  }
  if (welcome_bar_ != nullptr) {
    welcome_bar_->SetThemeDark(dark);
  }
  if (activity_bar_ != nullptr) {
    activity_bar_->SetThemeDark(dark);
  }
  if (tab_bar_ != nullptr) {
    tab_bar_->SetThemeDark(dark);
    for (int i = 1; i < tab_bar_->count() && i < static_cast<int>(tabs_.size());
         ++i) {
      if (!tabs_[i]->path.isEmpty()) {
        tab_bar_->setTabIcon(i, ThemeIcon(QStringLiteral("file"), dark));
      }
    }
  }
  if (title_leading_widget_ != nullptr) {
    QPalette strip = title_leading_widget_->palette();
    strip.setColor(QPalette::Window, ChromeColorsFor(dark).background);
    title_leading_widget_->setPalette(strip);
  }
  // Chrome 的标签条与标题栏同一条框架色；整条标题栏一起铺
  if (title_bar_ != nullptr) {
    title_bar_->setAutoFillBackground(true);
    QPalette chrome = title_bar_->palette();
    chrome.setColor(QPalette::Window, ChromeColorsFor(dark).background);
    title_bar_->setPalette(chrome);
  }
  UpdateBookmarkUi();
  if (side_panel_ != nullptr) {
    side_panel_->SetThemeDark(dark);
  }
  if (welcome_ != nullptr) {
    welcome_->SetThemeDark(dark);
  }
  // 弹层默认隐藏，也一并换掉，下次打开即正确
  if (quick_open_ != nullptr) {
    quick_open_->SetThemeDark(dark);
  }
}

bool MdViewerWindow::ThemeDark() const {
  return settings_ != nullptr && settings_->dark_theme;
}

void MdViewerWindow::ToggleTheme() {
  const bool dark = qApp->property("_q_colorscheme").toInt() == 1;
  ApplyTheme(dark ? MarkdownTheme::Light : MarkdownTheme::Dark);
}

void MdViewerWindow::ApplyWindowMaterial(WindowMaterial material,
                                         bool announce) {
  window_material_ = material;
  if (settings_panel_ != nullptr) {
    settings_panel_->SetWindowMaterial(material);
  }
  UpdateMaterialActions();
#ifdef Q_OS_WIN
  // 只切 backdrop 不够：关掉材质后调色板若仍透明，窗口会露底。
  // _q_widget_mode 与样式要一起切
  qApp->setProperty("_q_widget_mode",
                    material == WindowMaterial::kNone ? 0 : 2);
  qApp->setStyle(new FluentUI3Style);
  qApp->setPalette(qApp->style()->standardPalette());
  SetWindowBackdrop(QString());
  if (material != WindowMaterial::kNone) {
    QString key;
    if (material == WindowMaterial::kAcrylic) {
      key = QStringLiteral("acrylic-material");
    } else {
      const QOperatingSystemVersion windows_11(QOperatingSystemVersion::Windows,
                                               10, 0, 22000);
      key = QOperatingSystemVersion::current() >= windows_11
                ? QStringLiteral("mica")
                : QStringLiteral("dwm-blur");
    }
    if (!SetWindowBackdrop(key)) {
      SetWindowBackdrop(QStringLiteral("dwm-blur"));
    }
  }
  if (announce) {
    const QString message = material == WindowMaterial::kAcrylic
                                ? tr("Window material: acrylic")
                            : material == WindowMaterial::kNone
                                ? tr("Window material: off")
                                : tr("Window material: auto (Mica)");
    statusBar()->showMessage(message, 3000);
  }
#else
  Q_UNUSED(announce);
#endif
  RefreshChromePalette();
  UpdateReadingSurface();
}

void MdViewerWindow::UpdateMaterialActions() {
  if (actions_.material_auto == nullptr) {
    return;
  }
  actions_.material_auto->setChecked(window_material_ == WindowMaterial::kAuto);
  actions_.material_acrylic->setChecked(window_material_ ==
                                        WindowMaterial::kAcrylic);
  actions_.material_none->setChecked(window_material_ == WindowMaterial::kNone);
}

void MdViewerWindow::RefreshChromePalette() {
  const QPalette app_palette = qApp->palette();
  auto apply_palette = [](QWidget* root, const QPalette& palette) {
    if (root == nullptr) {
      return;
    }
    root->setPalette(palette);
    const QList<QWidget*> children = root->findChildren<QWidget*>();
    for (QWidget* child : children) {
      child->setPalette(palette);
    }
  };
  setPalette(app_palette);
  apply_palette(statusBar(), app_palette);
  apply_palette(side_panel_, app_palette);
  apply_palette(outline_column_, app_palette);
}

void MdViewerWindow::ApplyReadingChromeTo(MarkdownView* view) {
  if (view == nullptr || settings_ == nullptr) {
    return;
  }
  const bool prototype = settings_->reading_style == ReadingStyle::kPrototype;
  view->SetThemeStyleSheet(
      MarkdownTheme::Light,
      prototype ? LoadTextResource(
                      QStringLiteral(":/resources/themes/viewer-light.css"))
                : QString());
  view->SetThemeStyleSheet(
      MarkdownTheme::Dark,
      prototype ? LoadTextResource(
                      QStringLiteral(":/resources/themes/viewer-dark.css"))
                : QString());
  const bool transparent = prototype && settings_->transparent_reading;
  view->SetPageBackground(transparent ? QColor(Qt::transparent)
                                      : ReadingPageColor());
}

void MdViewerWindow::ApplyReadingStyle() {
  if (settings_ == nullptr) {
    return;
  }
  for (const auto& tab : tabs_) {
    ApplyReadingChromeTo(tab->view);
  }
  ApplyReaderAppearance();
  UpdateReadingSurface();
}

QColor MdViewerWindow::ReadingPageColor() const {
  if (settings_ != nullptr && settings_->dark_theme) {
    return QColor(0x0d, 0x11, 0x17);
  }
  return QColor(0xff, 0xff, 0xff);
}

void MdViewerWindow::UpdateReadingSurface() {
  if (settings_ == nullptr) {
    return;
  }
  const bool transparent =
      settings_->reading_style == ReadingStyle::kPrototype &&
      settings_->transparent_reading;
  const QColor color =
      transparent ? QColor(Qt::transparent) : ReadingPageColor();
  for (const auto& tab : tabs_) {
    if (tab->view != nullptr) {
      tab->view->SetPageBackground(color);
    }
  }
}

ReaderPreferences MdViewerWindow::CurrentReaderPreferences() const {
  ReaderPreferences preferences;
  if (settings_ == nullptr) {
    return preferences;
  }
  preferences.font_size = settings_->reading_font_size;
  preferences.line_height_percent = settings_->reading_line_height_percent;
  preferences.measure_px = settings_->reading_measure_px;
  return preferences;
}

void MdViewerWindow::ApplyReaderAppearance() {
  const bool prototype = settings_ != nullptr &&
                         settings_->reading_style == ReadingStyle::kPrototype;
  const ReaderPreferences preferences = CurrentReaderPreferences();
  const QString css = ReaderAppearanceCss(preferences, prototype);
  for (const auto& tab : tabs_) {
    if (tab->view == nullptr) {
      continue;
    }
    tab->view->AppendThemeStyleSheet(MarkdownTheme::Light, css);
    tab->view->AppendThemeStyleSheet(MarkdownTheme::Dark, css);
  }
}

void MdViewerWindow::ShowSettings() {
  if (settings_dialog_ == nullptr) {
    return;
  }
  if (settings_panel_ != nullptr) {
    settings_panel_->SetView(CurrentView());
  }
  settings_dialog_->show();
  settings_dialog_->raise();
  settings_dialog_->activateWindow();
}

void MdViewerWindow::ShowReaderAppearance() {
  if (appearance_popup_ == nullptr) {
    return;
  }
  appearance_popup_->SetPreferences(CurrentReaderPreferences());
  appearance_popup_->OpenNear(document_bar_->appearance_button());
}

void MdViewerWindow::ShowWelcome() {
  if (welcome_ == nullptr) {
    return;
  }
  if (active_tab_ != 0) {
    ActivateTab(0);
  }
  pages_->setCurrentWidget(welcome_);
  welcome_->SetRecentFiles(settings_->recent_files, root_dir_);
  if (appearance_popup_ != nullptr) {
    appearance_popup_->hide();
  }
  // 启动时就停在欢迎标签时 ActivateTab(0) 会提前返回，工具条不会自己
  // 切页；这里统一补一次，保证欢迎栏与文档栏始终跟当前标签一致
  UpdateShellForActiveTab();
}

void MdViewerWindow::HideWelcome() { UpdateOutlineVisibility(); }

void MdViewerWindow::UpdateReadingProgress(int y) {
  ReaderPage* page = CurrentPage();
  if (page == nullptr || CurrentView() == nullptr) {
    return;
  }
  ReadingProgressBar* progress_bar = page->progress();
  QToolButton* back_to_top = page->back_to_top();
  if (!CurrentView()->has_document() || !CurrentView()->isVisible()) {
    progress_bar->SetProgress(0.0);
    back_to_top->hide();
    return;
  }
  const int document_height = CurrentView()->DocumentHeight();
  const int viewport_height = CurrentView()->ViewportHeight();
  const int max_scroll = qMax(1, document_height - viewport_height);
  const qreal progress = qBound(0.0, qreal(y) / max_scroll, 1.0);
  progress_bar->SetProgress(progress);
  // 短文档的滚动范围可能还不到一屏；按约 200 设备像素判断更可靠。
  // y 是虚拟像素，缩放后要乘回去。
  const qreal zoom = qMax<qreal>(0.1, CurrentView()->zoom());
  const bool show = y * zoom > 200.0;
  back_to_top->setVisible(show);
}

void MdViewerWindow::ScheduleReadingProgressRefresh() {
  QTimer::singleShot(0, this, [this] {
    if (CurrentView() != nullptr) {
      UpdateReadingProgress(CurrentView()->ScrollPosition());
    }
  });
}

void MdViewerWindow::UpdateReadingTime() {
  if (reading_time_label_ == nullptr || CurrentView() == nullptr) {
    return;
  }
  if (!CurrentView()->has_document() || CurrentView()->is_rendering()) {
    reading_time_label_->clear();
    return;
  }
  const int chars = CurrentView()->document_stats().source_chars;
  if (chars <= 0) {
    reading_time_label_->clear();
    return;
  }
  const int minutes = qMax(1, (chars + 499) / 500);
  reading_time_label_->setText(tr("about %1 min").arg(minutes));
}

void MdViewerWindow::ScrollToTop() {
  if (CurrentView() == nullptr) {
    return;
  }
  if (scroll_animation_ == nullptr) {
    CurrentView()->SetScrollPosition(0);
    return;
  }
  scroll_animation_->stop();
  scroll_animation_->setStartValue(CurrentView()->ScrollPosition());
  scroll_animation_->setEndValue(0);
  scroll_animation_->start();
}

bool MdViewerWindow::eventFilter(QObject* watched, QEvent* event) {
  if (watched == pages_ && event->type() == QEvent::Resize) {
    ScheduleReadingProgressRefresh();
  }
  if (watched == document_find_edit_ && event->type() == QEvent::KeyPress) {
    auto* key = static_cast<QKeyEvent*>(event);
    if (key->key() == Qt::Key_Escape) {
      search_controller_->StopDocumentFind();
      {
        const QSignalBlocker blocker(document_find_edit_);
        document_find_edit_->clear();
      }
      if (CurrentView() != nullptr) {
        CurrentView()->ClearSelection();
        CurrentView()->setFocus();
      }
      return true;
    }
    const bool enter =
        key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter;
    if (enter && key->modifiers().testFlag(Qt::ShiftModifier)) {
      search_controller_->FindDocumentNow(document_find_edit_->text(), true);
      return true;
    }
  }
  return QMainWindow::eventFilter(watched, event);
}

bool MdViewerWindow::ShowFile(const QString& path, OpenReason reason,
                              OpenIntent intent) {
  const QFileInfo info(path);
  const QString absolute_path = info.absoluteFilePath();
  if (pending_search_hit_.has_value() &&
      pending_search_hit_->path != absolute_path) {
    pending_search_hit_.reset();
  }
  // 已打开的文件切过去，不产生重复标签；历史导航例外，它在当前标签内换篇
  if (intent != OpenIntent::kHistory) {
    const int existing = FindTabIndex(absolute_path);
    if (existing > 0) {
      ActivateTab(existing);
      return true;
    }
  }
  if (reason == OpenReason::kUser && info.size() > kLargeDocumentBytes &&
      !large_file_prompted_.contains(absolute_path)) {
    const QMessageBox::StandardButton answer = QMessageBox::warning(
        this, tr("Large File"),
        tr("This document is about %1; the window may be unresponsive while "
           "rendering. Open it anyway?")
            .arg(QLocale().formattedDataSize(info.size())),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes) {
      return false;
    }
    large_file_prompted_.insert(absolute_path);
  }
  DocumentTab* target = CurrentTab();
  if (intent == OpenIntent::kNewTab || target == nullptr ||
      target->path.isEmpty()) {
    target = CreateDocumentTab();
    ActivateTab(static_cast<int>(tabs_.size()) - 1);
    target = CurrentTab();
  }
  if (target == nullptr || target->view == nullptr) {
    return false;
  }
  if (!target->view->current_file().isEmpty() &&
      target->view->current_file() != absolute_path) {
    RememberCurrentPosition();
  }
  target->pending_restore_path = absolute_path;
  target->scroll_position = position_controller_->ScrollFor(absolute_path);
  target->pending_restore = target->scroll_position >= 0;
  if (!target->view->LoadFile(absolute_path)) {
    SetTabLoading(target, false);
    target->pending_restore = false;
    target->pending_restore_path.clear();
    // 视图里已经提交了错误页；欢迎页正显示时必须让位，否则用户
    // 只看得到 5 秒状态栏消息
    HideWelcome();
    return false;
  }
  HideWelcome();
  SetTabLoading(target, true);
  // 旧文档还在屏幕上，而新文档已在后台渲染；先给一个明确的等待页
  target->view->ShowPlaceholder(tr("Rendering…"));
  target->path = absolute_path;
  // 标题栏、面包屑、状态栏路径统一由当前标签刷新
  UpdateShellForActiveTab();
  // 打开任意文件都算重新有了工作区
  session_controller_->MarkWorkspaceOpened(absolute_path);
  SaveTabSession();
  // 最近记录立即落盘：即使随后异常退出，也不会丢这次打开
  settings_->AddRecentFile(absolute_path);
  RefreshRecentMenu();
  if (reason == OpenReason::kRestore) {
    statusBar()->showMessage(
        tr("Restored %1 (%2)")
            .arg(info.fileName(), QLocale().formattedDataSize(info.size())),
        3000);
  }
  return true;
}

void MdViewerWindow::UpdateBookmarkUi() {
  const bool starred = !CurrentTab()->path.isEmpty() &&
                       bookmark_controller_->Contains(CurrentTab()->path);
  QToolButton* star =
      document_bar_ != nullptr ? document_bar_->star_button() : nullptr;
  if (star != nullptr) {
    const QSignalBlocker blocker(star);
    star->setChecked(starred);
    // 实心星是语义图标（金色）；空心星走主题套
    star->setIcon(starred
                      ? QIcon(QStringLiteral(":/resources/svg/star-filled.svg"))
                      : ThemeIcon(QStringLiteral("star-outline"), ThemeDark()));
    star->setToolTip(starred ? tr("Unbookmark (Ctrl+D)")
                             : tr("Bookmark Current Document (Ctrl+D)"));
  }
  if (side_panel_ != nullptr) {
    side_panel_->SetBookmarks(bookmark_controller_->items(), root_dir_);
  }
}

void MdViewerWindow::SetBookmarkChecked(bool starred) {
  const QString path = CurrentTab()->path;
  if (path.isEmpty()) {
    UpdateBookmarkUi();
    statusBar()->showMessage(tr("Open a document before bookmarking"), 3000);
    return;
  }
  if (bookmark_controller_->Contains(path) != starred) {
    bookmark_controller_->Toggle(path, root_dir_);
  }
  bookmark_controller_->Save();
  UpdateBookmarkUi();
  statusBar()->showMessage(starred ? tr("Bookmarked") : tr("Bookmark removed"),
                           2000);
}

void MdViewerWindow::ToggleBookmarkForPath(const QString& path) {
  if (path.isEmpty()) {
    return;
  }
  const bool starred = !bookmark_controller_->Contains(path);
  bookmark_controller_->Toggle(path, root_dir_);
  bookmark_controller_->Save();
  UpdateBookmarkUi();
  statusBar()->showMessage(starred ? tr("Bookmarked") : tr("Bookmark removed"),
                           2000);
}

void MdViewerWindow::OpenBookmark(const QString& path) {
  if (!QFileInfo(path).isFile()) {
    statusBar()->showMessage(
        tr("File no longer exists: %1").arg(QFileInfo(path).fileName()), 4000);
    return;
  }
  const bool outside_root = !IsPathUnderRoot(path, root_dir_);
  const QString absolute = QFileInfo(path).absoluteFilePath();
  if (!ShowFile(absolute, OpenReason::kUser, OpenIntent::kNewTab)) {
    return;
  }
  PushHistory(absolute, QString());
  if (outside_root) {
    statusBar()->showMessage(tr("The bookmark is outside the current folder"),
                             4000);
  }
}

void MdViewerWindow::RefreshRecentMenu() {
  if (recent_menu_ == nullptr) {
    return;
  }
  recent_menu_->clear();
  if (settings_->recent_files.isEmpty()) {
    QAction* empty = recent_menu_->addAction(tr("(none)"));
    empty->setEnabled(false);
    return;
  }
  for (const QString& path : settings_->recent_files) {
    QAction* action = recent_menu_->addAction(QFileInfo(path).fileName());
    action->setToolTip(path);
    connect(action, &QAction::triggered, this, [this, path] {
      NavigateTo(path, QString(), /*record_history=*/true,
                 OpenIntent::kReplace);
    });
  }
  recent_menu_->addSeparator();
  QAction* clear = recent_menu_->addAction(tr("Clear Recent Files"));
  connect(clear, &QAction::triggered, this, [this] {
    settings_->recent_files.clear();
    settings_->SaveRecent();
    RefreshRecentMenu();
  });
}

void MdViewerWindow::RefreshRecentRootsMenu() {
  if (recent_roots_menu_ == nullptr || settings_ == nullptr) {
    return;
  }
  recent_roots_menu_->clear();
  if (settings_->recent_roots.isEmpty()) {
    QAction* empty = recent_roots_menu_->addAction(tr("(none)"));
    empty->setEnabled(false);
    return;
  }
  for (const QString& root : settings_->recent_roots) {
    QAction* action =
        recent_roots_menu_->addAction(QDir::toNativeSeparators(root));
    action->setToolTip(root);
    connect(action, &QAction::triggered, this,
            [this, root] { OpenRecentRoot(root); });
  }
  recent_roots_menu_->addSeparator();
  QAction* clear = recent_roots_menu_->addAction(tr("Clear Recent Workspaces"));
  connect(clear, &QAction::triggered, this, [this] {
    settings_->recent_roots.clear();
    settings_->SaveRecentRoots();
    RefreshRecentRootsMenu();
  });
}

void MdViewerWindow::OpenRecentRoot(const QString& root) {
  if (!QFileInfo(root).isDir()) {
    statusBar()->showMessage(tr("Workspace does not exist: %1").arg(root),
                             4000);
    return;
  }
  NavigateTo(root, QString(), /*record_history=*/true, OpenIntent::kReplace);
}

void MdViewerWindow::SaveSettings() {
  // 会话与几何只归主窗口；次窗口退出不写任何东西
  if (!session_controller_->IsOwner() || CurrentView() == nullptr) {
    return;
  }
  RememberCurrentPosition();
  WriteTabSession();
  session_controller_->SaveGeometry(saveGeometry());
  if (activity_bar_ != nullptr) {
    layout_.view = activity_bar_->active_view();
  }
  if (side_panel_ != nullptr) {
    layout_.sidebar_visible = side_panel_->isVisible();
    if (side_panel_->isVisible()) {
      layout_.sidebar_width = side_panel_->width();
    }
  }
  if (outline_column_ != nullptr && outline_column_->isVisible()) {
    layout_.outline_width = outline_column_->width();
  }
  session_controller_->SaveShellLayout(layout_);
  CaptureGlobalSettings(CurrentTab());
  settings_->window_material = window_material_;
  session_controller_->SaveAll();
}

void MdViewerWindow::UpdateZoomLabel(qreal factor) {
  if (document_bar_ != nullptr) {
    document_bar_->zoom_reset_button()->setText(
        QStringLiteral("%1%").arg(qRound(factor * 100.0)));
  }
}

void MdViewerWindow::ReportRenderStatus() {
  const MarkdownRendererStatus now = CurrentView()->renderer_status();
  QStringList failed_engines;
  QStringList loaded_engines;
  auto note_engine = [&](const MarkdownEngineStatus& before,
                         const MarkdownEngineStatus& after,
                         const QString& name) {
    if (!before.permanently_failed && after.permanently_failed) {
      failed_engines << name;
    } else if (!before.loaded && after.loaded) {
      loaded_engines
          << QStringLiteral("%1 %2 ms").arg(name).arg(after.first_load_ms);
    }
  };
  note_engine(last_render_status_.highlighter, now.highlighter,
              tr("Highlighting"));
  note_engine(last_render_status_.formula, now.formula, tr("Formulas"));
  note_engine(last_render_status_.mermaid, now.mermaid,
              QStringLiteral("Mermaid"));

  auto delta = [](quint64 before, quint64 after) {
    return after > before ? after - before : quint64(0);
  };
  const quint64 highlight_failures =
      delta(last_render_status_.highlight_context.failures,
            now.highlight_context.failures);
  const quint64 formula_failures =
      delta(last_render_status_.formula_context.failures,
            now.formula_context.failures);
  const quint64 mermaid_failures =
      delta(last_render_status_.mermaid_context.failures,
            now.mermaid_context.failures);
  last_render_status_ = now;

  if (!failed_engines.isEmpty()) {
    statusBar()->showMessage(tr("Failed to load render engines: %1")
                                 .arg(failed_engines.join(tr(", "))),
                             5000);
    return;
  }
  if (highlight_failures + formula_failures + mermaid_failures > 0) {
    QStringList parts;
    if (highlight_failures > 0) {
      parts << tr("Highlighting %1").arg(highlight_failures);
    }
    if (formula_failures > 0) {
      parts << tr("Formulas %1").arg(formula_failures);
    }
    if (mermaid_failures > 0) {
      parts << QStringLiteral("Mermaid %1").arg(mermaid_failures);
    }
    statusBar()->showMessage(
        tr("Render failures this run: %1").arg(parts.join(tr(", "))), 5000);
    return;
  }
  if (!loaded_engines.isEmpty()) {
    statusBar()->showMessage(
        tr("Render engines loaded: %1").arg(loaded_engines.join(tr(", "))),
        3000);
  }
}

QMenu* MdViewerWindow::createPopupMenu() {
  // 侧栏由活动栏管理，顶部工具条不允许关闭；右键菜单直接不给
  return nullptr;
}

void MdViewerWindow::closeEvent(QCloseEvent* event) {
  SaveSettings();
  QMainWindow::closeEvent(event);
}

void MdViewerWindow::resizeEvent(QResizeEvent* event) {
  QMainWindow::resizeEvent(event);
  UpdateOutlineVisibility();
}

void MdViewerWindow::showEvent(QShowEvent* event) {
  QMainWindow::showEvent(event);
  // 构造期 splitter 还没有真实宽度，setSizes 会被后续布局覆盖；首次显示
  // 之后再落一次保存值，侧栏宽度/收起状态才能按会话恢复。
  if (!shell_sizes_applied_) {
    shell_sizes_applied_ = true;
    ApplySplitterSizes();
  }
}

void MdViewerWindow::dragEnterEvent(QDragEnterEvent* event) {
  if (DroppablePaths(event->mimeData()).isEmpty()) {
    event->ignore();
    return;
  }
  event->acceptProposedAction();
}

void MdViewerWindow::dragMoveEvent(QDragMoveEvent* event) {
  // 与 dragEnter 同一判据：拖到子控件上方时仍保持可放下
  if (DroppablePaths(event->mimeData()).isEmpty()) {
    event->ignore();
    return;
  }
  event->acceptProposedAction();
}

void MdViewerWindow::dropEvent(QDropEvent* event) {
  const QStringList paths = DroppablePaths(event->mimeData());
  if (paths.isEmpty()) {
    event->ignore();
    return;
  }
  event->acceptProposedAction();
  OpenPath(paths.first());
  if (paths.size() > 1) {
    statusBar()->showMessage(tr("Opened the first dropped item: %1")
                                 .arg(QFileInfo(paths.first()).fileName()),
                             5000);
  }
}

void MdViewerWindow::FocusDocumentFind() {
  if (CurrentTab() == nullptr || CurrentTab()->path.isEmpty()) {
    statusBar()->showMessage(tr("Open a document first"), 2000);
    return;
  }
  UpdateFindControls();
  document_find_edit_->setFocus();
  document_find_edit_->selectAll();
}

void MdViewerWindow::FocusWorkspaceSearch() {
  EnsureSideView(SideView::kSearch);
  if (workspace_search_edit_ == nullptr ||
      !workspace_search_edit_->isEnabled()) {
    statusBar()->showMessage(tr("Open a folder first"), 2000);
    return;
  }
  workspace_search_edit_->setFocus();
  workspace_search_edit_->selectAll();
}

void MdViewerWindow::ShowSideView(SideView view) {
  if (side_panel_ == nullptr || activity_bar_ == nullptr) {
    return;
  }
  // 点当前已选中的视图 = 收起/展开侧栏
  if (activity_bar_->active_view() == view && side_panel_->isVisible()) {
    ApplySidebarVisible(false);
    return;
  }
  EnsureSideView(view);
}

void MdViewerWindow::EnsureSideView(SideView view) {
  if (side_panel_ == nullptr || activity_bar_ == nullptr) {
    return;
  }
  ApplySidebarVisible(true);
  ApplySplitterSizes();
  ApplySideView(view);
  layout_.view = view;
}

void MdViewerWindow::ApplySidebarVisible(bool visible) {
  if (side_panel_ == nullptr || applying_sidebar_) {
    return;
  }
  applying_sidebar_ = true;
  // 收起前记宽度；启动恢复路径（面板尚未显示）不能拿默认宽度覆盖存档
  if (!visible && side_panel_->isVisible()) {
    layout_.sidebar_width = side_panel_->width();
  }
  side_panel_->setVisible(visible);
  layout_.sidebar_visible = visible;
  // setChecked 会发 toggled/clicked，必须阻断，否则会绕回本函数
  if (activity_bar_ != nullptr &&
      activity_bar_->sidebar_toggle_button() != nullptr) {
    const QSignalBlocker blocker(activity_bar_->sidebar_toggle_button());
    activity_bar_->sidebar_toggle_button()->setChecked(visible);
  }
  if (actions_.toggle_sidebar != nullptr) {
    const QSignalBlocker blocker(actions_.toggle_sidebar);
    actions_.toggle_sidebar->setChecked(visible);
  }
  // 显隐会改变左外壳最大宽度与 splitter 尺寸，统一在这里落一次布局
  ApplySplitterSizes();
  applying_sidebar_ = false;
}

bool MdViewerWindow::SetWindowBackdrop(const QString& key) {
  if (window_agent_ == nullptr) {
    return false;
  }
  static const QStringList kBackdropAttributes = {
      QStringLiteral("dwm-blur"), QStringLiteral("acrylic-material"),
      QStringLiteral("mica"), QStringLiteral("mica-alt")};
  for (const QString& attribute : kBackdropAttributes) {
    window_agent_->setWindowAttribute(attribute, false);
  }
  if (key.isEmpty() || key == QStringLiteral("none")) {
    return true;
  }
  return window_agent_->setWindowAttribute(key, true);
}

void MdViewerWindow::ApplySideView(SideView view) {
  if (activity_bar_ != nullptr) {
    activity_bar_->SetActiveView(view);
  }
  // 标题与页切换都在 SidePanel 内部，页序即 SideView 数值
  if (side_panel_ != nullptr) {
    side_panel_->ShowView(view);
  }
}

void MdViewerWindow::ApplySplitterSizes() {
  if (main_splitter_ == nullptr) {
    return;
  }
  // 隐藏的列会被 QSplitter 忽略，正文吃掉剩余宽度
  main_splitter_->setSizes({800, layout_.outline_width});
  if (shell_splitter_ == nullptr || left_shell_ == nullptr) {
    return;
  }
  const bool sidebar = side_panel_ != nullptr && side_panel_->isVisible();
  // 收起后左外壳收缩到活动栏宽度；展开时恢复会话宽度
  left_shell_->setMaximumWidth(sidebar ? QWIDGETSIZE_MAX : 42);
  shell_splitter_->setSizes({42 + (sidebar ? layout_.sidebar_width : 0), 800});
}

void MdViewerWindow::UpdateOutlineVisibility() {
  if (outline_column_ == nullptr) {
    return;
  }
  const bool welcome = CurrentTab() == nullptr || CurrentTab()->path.isEmpty();
  const bool should_show =
      layout_.outline_visible && !welcome && width() >= 980;
  if (outline_column_->isVisible() == should_show) {
    return;
  }
  if (outline_column_->isVisible()) {
    layout_.outline_width = outline_column_->width();
    outline_column_->hide();
  } else {
    outline_column_->show();
    ApplySplitterSizes();
  }
}

void MdViewerWindow::FindInDocument(const QString& query, bool backward) {
  MarkdownView* view = CurrentView();
  if (view == nullptr || query.isEmpty()) {
    ClearFindCount();
    return;
  }
  if (!view->has_document()) {
    ClearFindCount();
    statusBar()->showMessage(tr("Open a document first"), 2000);
    return;
  }
  if (view->is_rendering()) {
    ClearFindCount();
    statusBar()->showMessage(
        tr("Document is still rendering; try find again later"), 2000);
    return;
  }
  bool wrapped = false;
  int match_index = -1;
  int match_count = 0;
  const MarkdownView::FindFlags flags =
      backward ? MarkdownView::FindBackward : MarkdownView::FindNoFlag;
  if (!view->FindText(query, flags, &wrapped, /*incremental=*/false,
                      &match_index, &match_count)) {
    UpdateFindCount(-1, 0);
    statusBar()->showMessage(tr("Not found"), 2000);
    return;
  }
  UpdateFindCount(match_index, match_count);
  if (wrapped) {
    statusBar()->showMessage(
        backward ? tr("Wrapped to the end") : tr("Wrapped to the beginning"),
        2000);
  }
}

void MdViewerWindow::UpdateFindCount(int match_index, int match_count) {
  if (document_bar_ != nullptr) {
    document_bar_->SetFindCount(match_index, match_count);
  }
}

void MdViewerWindow::ClearFindCount() {
  if (document_bar_ != nullptr) {
    document_bar_->ClearFindCount();
  }
}

void MdViewerWindow::UpdateFindControls() {
  if (document_find_edit_ == nullptr) {
    return;
  }
  const bool has_document =
      CurrentTab() != nullptr && !CurrentTab()->path.isEmpty() &&
      CurrentView() != nullptr && CurrentView()->has_document();
  const bool rendering =
      CurrentView() != nullptr && CurrentView()->is_rendering();
  // 查询文字保留，只禁输入：渲染中和欢迎态都不该往正文里找
  document_find_edit_->setEnabled(has_document && !rendering);
  if (document_bar_ != nullptr) {
    document_bar_->SetFindNavigationEnabled(has_document && !rendering);
  }
  if (!has_document || rendering) {
    ClearFindCount();
  }
}

void MdViewerWindow::OpenSearchHit(const SearchHit& hit) {
  if (!QFileInfo(hit.file_path).isFile()) {
    statusBar()->showMessage(tr("File no longer exists"), 4000);
    return;
  }
  QString query = search_controller_ != nullptr
                      ? search_controller_->active_query()
                      : QString();
  if (query.isEmpty()) {
    query = workspace_search_edit_->text();
  }
  // 文件已在别的标签里：切过去再跳，别走 NavigateTo 的去重分支（它会把
  // 目标标签滚回顶部，还会让 pending_search_hit_ 悬着）
  const int open_index =
      FindTabIndex(QFileInfo(hit.file_path).absoluteFilePath());
  if (open_index > 0 && open_index != active_tab_) {
    ActivateTab(open_index);
    if (CurrentView()->is_rendering() &&
        CurrentView()->pending_file() == hit.file_path) {
      pending_search_hit_ = PendingJump{hit.file_path, hit.file_ordinal, query};
      return;
    }
    pending_search_hit_.reset();
    JumpToSearchHit(hit.file_ordinal, query);
    return;
  }
  if (hit.file_path == CurrentView()->current_file()) {
    // 当前文档正在重渲染：等 renderFinished 再跳，别取消这次渲染
    if (CurrentView()->is_rendering() &&
        CurrentView()->pending_file() == hit.file_path) {
      pending_search_hit_ = PendingJump{hit.file_path, hit.file_ordinal, query};
      return;
    }
    // 后台正在换到别的文档：取消换篇，留在当前文档里跳
    if (CurrentView()->is_rendering() &&
        !CurrentView()->pending_file().isEmpty()) {
      CurrentView()->CancelPendingRender();
      CurrentTab()->pending_fragment.clear();
      CurrentTab()->path = CurrentView()->current_file();
      UpdateBookmarkUi();
    }
    pending_search_hit_.reset();
    JumpToSearchHit(hit.file_ordinal, query);
    return;
  }
  pending_search_hit_ = PendingJump{hit.file_path, hit.file_ordinal, query};
  NavigateTo(hit.file_path, QString(), /*record_history=*/true);
  // 搜索跳转自己管滚动，别让阅读位置恢复把选区顶掉
  CurrentTab()->pending_restore = false;
  CurrentTab()->pending_restore_path.clear();
}

void MdViewerWindow::JumpToSearchHit(int ordinal, const QString& query) {
  if (query.isEmpty()) {
    return;
  }
  // 从顶部开始数，序号才和源码命中顺序大致对齐
  CurrentView()->ClearSelection();
  CurrentView()->SetScrollPosition(0);
  bool wrapped = false;
  for (int i = 0; i <= ordinal; ++i) {
    if (!CurrentView()->FindText(query, MarkdownView::FindNoFlag, &wrapped,
                                 /*incremental=*/false)) {
      statusBar()->showMessage(tr("Cannot locate the hit precisely; stopped at "
                                  "the top of the document"),
                               4000);
      return;
    }
  }
}

void MdViewerWindow::ExportHtml() {
  if (CurrentView()->is_rendering()) {
    statusBar()->showMessage(
        tr("Document is still rendering; try export again later"), 3000);
    return;
  }
  if (!CurrentView()->has_document()) {
    statusBar()->showMessage(tr("Open a Markdown document before exporting"),
                             3000);
    return;
  }
  QString default_path;
  if (!CurrentView()->current_file().isEmpty()) {
    const QFileInfo info(CurrentView()->current_file());
    default_path = info.absolutePath() + QLatin1Char('/') +
                   info.completeBaseName() + QStringLiteral(".html");
  } else {
    default_path = QDir::homePath() + QStringLiteral("/untitled.html");
  }

  QString path = QFileDialog::getSaveFileName(
      this, tr("Export HTML"), default_path, tr("HTML Files (*.html *.htm)"));
  if (path.isEmpty()) {
    return;
  }
  if (!path.endsWith(QStringLiteral(".html"), Qt::CaseInsensitive) &&
      !path.endsWith(QStringLiteral(".htm"), Qt::CaseInsensitive)) {
    path += QStringLiteral(".html");
  }

  QString error;
  if (!CurrentView()->ExportHtml(path, &error)) {
    statusBar()->showMessage(
        error.isEmpty() ? tr("Failed to export HTML") : error, 5000);
    return;
  }
  statusBar()->showMessage(tr("Exported: %1").arg(QFileInfo(path).fileName()),
                           5000);
}

void MdViewerWindow::OpenImage(const QUrl& url) { ShowImagePreview(url); }

void MdViewerWindow::ShowImageContextMenu(const QPoint& global_pos,
                                          const QUrl& url) {
  const QPixmap pixmap = CurrentView()->ImagePixmap(url);
  const std::optional<MarkdownImageInfo> info = CurrentView()->ImageInfo(url);

  QMenu menu(this);
  QAction* preview = menu.addAction(tr("Zoom In"));
  preview->setObjectName(QStringLiteral("imagePreviewAction"));
  preview->setEnabled(!pixmap.isNull());
  QAction* save = menu.addAction(tr("Save Image As…"));
  save->setObjectName(QStringLiteral("imageSaveAsAction"));
  save->setEnabled(!pixmap.isNull());
  QAction* source = nullptr;
  if (info.has_value()) {
    menu.addSeparator();
    source = menu.addAction(tr("View Source"));
    source->setObjectName(QStringLiteral("imageSourceAction"));
  }

  QAction* chosen = menu.exec(global_pos);
  if (chosen == preview) {
    ShowImagePreview(url);
  } else if (chosen == save) {
    SaveImageAs(url);
  } else if (source != nullptr && chosen == source) {
    ShowImageSource(url);
  }
}

void MdViewerWindow::SaveImageAs(const QUrl& url) {
  const QPixmap pixmap = CurrentView()->ImagePixmap(url);
  if (pixmap.isNull()) {
    statusBar()->showMessage(tr("This image cannot be saved"), 3000);
    return;
  }
  QString start_dir = QDir::homePath();
  if (!CurrentView()->current_file().isEmpty()) {
    start_dir = QFileInfo(CurrentView()->current_file()).absolutePath();
  }
  QString path = QFileDialog::getSaveFileName(
      this, tr("Save Image As"), start_dir + QStringLiteral("/image.png"),
      tr("PNG Images (*.png);;JPEG Images (*.jpg *.jpeg);;"
         "All Files (*)"));
  if (path.isEmpty()) {
    return;
  }
  if (QFileInfo(path).suffix().isEmpty()) {
    path += QStringLiteral(".png");
  }
  if (!pixmap.save(path)) {
    statusBar()->showMessage(tr("Failed to save image"), 5000);
    return;
  }
  statusBar()->showMessage(
      tr("Image saved: %1").arg(QFileInfo(path).fileName()), 5000);
}

void MdViewerWindow::ShowImagePreview(const QUrl& url) {
  const QPixmap pixmap = CurrentView()->ImagePixmap(url);
  if (pixmap.isNull()) {
    statusBar()->showMessage(tr("This image cannot be previewed"), 3000);
    return;
  }
  const std::optional<MarkdownImageInfo> info = CurrentView()->ImageInfo(url);
  QString title = tr("Image Preview");
  if (info.has_value()) {
    title = info->kind == MarkdownImageKind::kFormula ? tr("Formula Preview")
                                                      : tr("Mermaid Diagram");
  }
  ImageLightbox dialog(pixmap, title, info.has_value(), this);
  int pending_action = 0;
  connect(&dialog, &ImageLightbox::sourceRequested, &dialog, [&] {
    pending_action = 1;
    dialog.accept();
  });
  connect(&dialog, &ImageLightbox::saveRequested, &dialog, [&] {
    pending_action = 2;
    dialog.accept();
  });
  dialog.exec();
  if (pending_action == 1) {
    ShowImageSource(url);
  } else if (pending_action == 2) {
    SaveImageAs(url);
  }
}

void MdViewerWindow::ShowImageSource(const QUrl& url) {
  const std::optional<MarkdownImageInfo> info = CurrentView()->ImageInfo(url);
  if (!info.has_value()) {
    return;
  }
  QString source = info->source;
  QString title = tr("Mermaid Source");
  if (info->kind == MarkdownImageKind::kFormula) {
    title = tr("Formula Source");
    const QString delimiter =
        info->display ? QStringLiteral("$$") : QStringLiteral("$");
    source = delimiter + source + delimiter;
  }
  SourceDialog dialog(title, source, this);
  dialog.exec();
}

void MdViewerWindow::ExportPdf() {
  if (CurrentView()->is_rendering()) {
    statusBar()->showMessage(
        tr("Document is still rendering; try export again later"), 3000);
    return;
  }
  if (!CurrentView()->has_document()) {
    statusBar()->showMessage(tr("Open a Markdown document before exporting"),
                             3000);
    return;
  }
  QString default_path;
  if (!CurrentView()->current_file().isEmpty()) {
    const QFileInfo info(CurrentView()->current_file());
    default_path = info.absolutePath() + QLatin1Char('/') +
                   info.completeBaseName() + QStringLiteral(".pdf");
  } else {
    default_path = QDir::homePath() + QStringLiteral("/untitled.pdf");
  }

  QString path = QFileDialog::getSaveFileName(
      this, tr("Export PDF"), default_path, tr("PDF Files (*.pdf)"));
  if (path.isEmpty()) {
    return;  // 用户取消
  }
  if (!path.endsWith(QStringLiteral(".pdf"), Qt::CaseInsensitive)) {
    path += QStringLiteral(".pdf");
  }

  // 分页与 print 媒体重排都在控件里（见 MarkdownView::Print 的注释）
  if (CurrentView()->is_rendering()) {
    statusBar()->showMessage(
        tr("Document is still rendering; try export again later"), 3000);
    return;
  }
  QPrinter printer(QPrinter::HighResolution);
  printer.setOutputFormat(QPrinter::PdfFormat);
  printer.setOutputFileName(path);
  CurrentView()->Print(&printer);

  statusBar()->showMessage(tr("Exported: %1").arg(QFileInfo(path).fileName()),
                           5000);
}

// SourceDialog 在 .cpp 里带 Q_OBJECT，AUTOMOC 要求包含 moc 产物
#include "md_viewer_window.moc"
