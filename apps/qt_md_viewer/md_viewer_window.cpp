// clang-format off
#include "md_viewer_window.h"

// Must precede windows.h: winuser.h defines FindText as FindTextA/W.
#include "markdown_view.h"

#include "markdown_render_context.h"

#include <fluenttitlebar.h>
#include <fluentui3style.h>
#include <fluentwindowframe.h>

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
#include <QDockWidget>
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
#include <QStackedWidget>
#include <QStatusBar>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QVariantAnimation>
#include <QWheelEvent>

#ifdef Q_OS_WIN
#include <windows.h>
// winuser.h maps FindText to FindTextA/W; MarkdownView's method is not that.
#undef FindText
#endif
// clang-format on

#include "document_index.h"
#include "document_tab_bar.h"
#include "documents_panel.h"
#include "image_lightbox.h"
#include "markdown_outline_widget.h"
#include "md_viewer_settings.h"
#include "quick_open.h"
#include "reader_appearance.h"
#include "reader_page.h"
#include "reading_progress.h"
#include "render_settings_panel.h"
#include "render_status_panel.h"
#include "settings_dialog.h"
#include "tab_model.h"
#include "text_search.h"
#include "theme_icons.h"
#include "welcome_view.h"

namespace {

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
    auto* copy = new QPushButton(QStringLiteral("复制"), this);
    copy->setObjectName(QStringLiteral("imageSourceCopy"));
    auto* close = new QPushButton(QStringLiteral("关闭"), this);
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
    : QMainWindow(parent), settings_(settings), session_owner_(session_owner) {
  Q_ASSERT(settings_ != nullptr);
  // 次窗口拿主窗口设置的只读副本，改动只影响自己、不落盘
  if (!session_owner_) {
    owned_settings_ = std::make_unique<MdViewerSettings>(*settings_);
    owned_settings_->read_only = true;
    settings_ = owned_settings_.get();
  }
  bookmarks_ = BookmarkStore::FromJson(settings_->bookmarks_json);
  positions_ = ReadingPositionStore::FromJson(settings_->positions_json);
  setAcceptDrops(true);
  createActions();
  initUi();
  initSignals();
  // 次窗口不继承主窗口的几何与 dock；它只负责一次性阅读
  if (session_owner_ && !settings_->window_geometry.isEmpty()) {
    restoreGeometry(settings_->window_geometry);
  }
  if (session_owner_ && !settings_->dock_state.isEmpty()) {
    restoreState(settings_->dock_state);
  }
  // 旧布局把大纲放在左侧；新默认是正文右侧整列，恢复后迁移一次
  if (outline_dock_ != nullptr &&
      dockWidgetArea(outline_dock_) != Qt::RightDockWidgetArea) {
    addDockWidget(Qt::RightDockWidgetArea, outline_dock_);
    outline_dock_->show();
  }
  resizeDocks({document_dock_, outline_dock_}, {300, 212}, Qt::Horizontal);
  CaptureWelcomeState();
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
  return tab != nullptr ? tab->page : nullptr;
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
  auto* view = new MarkdownView(pages_);
  tab->view = view;
  auto* page = new ReaderPage(view, pages_);
  tab->page = page;
  pages_->addWidget(page);
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
    tab_bar_->addTab(created->path.isEmpty()
                         ? QStringLiteral("新标签页")
                         : QFileInfo(created->path).fileName());
    tab_bar_->setTabToolTip(
        tab_bar_->count() - 1,
        created->path.isEmpty()
            ? QStringLiteral("新标签页")
            : DocumentDisplayTitle(root_dir_, created->path));
  }
  return created;
}

void MdViewerWindow::DetachPanelsFromTab(DocumentTab* tab) {
  if (tab == nullptr || tab != CurrentTab() || tabs_.empty()) {
    return;
  }
  MarkdownView* fallback = tabs_[0]->view;
  if (render_settings_ != nullptr && fallback != nullptr) {
    render_settings_->SetView(fallback);
  }
  if (render_status_ != nullptr && fallback != nullptr) {
    render_status_->SetView(fallback);
  }
}

void MdViewerWindow::ClearDocumentTabs() {
  // 活动标签的视图可能正被设置/状态面板引用
  DetachPanelsFromTab(CurrentTab());
  if (tab_bar_ != nullptr) {
    const QSignalBlocker blocker(tab_bar_);
    while (tab_bar_->count() > 1) {
      tab_bar_->removeTab(tab_bar_->count() - 1);
    }
  }
  for (int i = static_cast<int>(tabs_.size()) - 1; i >= 1; --i) {
    ReaderPage* page = tabs_[i]->page;
    if (page != nullptr) {
      pages_->removeWidget(page);
      // 立即删：deleteLater 期间视图还在，排队中的渲染信号会带着
      // 已析构的 DocumentTab 指针进来
      delete page;
    }
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
      statusBar()->showMessage(QStringLiteral("文件已更新，已重新加载"), 3000);
    }
  });
  connect(
      view, &MarkdownView::renderProgress, this,
      [this, tab](quint64 request_id, int completed, int total, int in_flight) {
        Q_UNUSED(in_flight);
        tab->render_request = request_id;
        if (tab != CurrentTab()) {
          return;
        }
        if (total > 0) {
          render_progress_->setRange(0, total);
          render_progress_->setValue(completed);
          render_progress_->setFormat(QStringLiteral("渲染 %v/%m"));
          render_progress_->show();
        } else {
          render_progress_->hide();
        }
      });
  connect(view, &MarkdownView::renderFinished, this,
          [this, tab, view](quint64) {
            tab->render_request = 0;
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
      statusBar()->showMessage(QStringLiteral("已复制代码块"), 2000);
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
            statusBar()->showMessage(
                QStringLiteral("图片加载失败：%1").arg(label), 5000);
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
            if (tab != CurrentTab()) {
              return;
            }
            statusBar()->showMessage(
                QStringLiteral("打开失败：%1（%2）")
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
      tab->page != nullptr ? static_cast<QWidget*>(tab->page) : welcome_;
  pages_->setCurrentWidget(page);
  if (tab->view != nullptr) {
    ApplyGlobalSettings(tab);
    if (render_settings_ != nullptr) {
      render_settings_->SetView(tab->view);
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

void MdViewerWindow::SyncTabSessionFields() {
  if (!session_owner_ || settings_ == nullptr) {
    return;
  }
  TabModel model;
  for (const auto& tab : tabs_) {
    model.Add(tab->path);
  }
  if (active_tab_ >= 0 && active_tab_ < model.count()) {
    model.SetActive(active_tab_);
  }
  settings_->open_tabs_json = model.Serialize();
  settings_->last_file =
      CurrentTab() != nullptr ? CurrentTab()->path : QString();
}

void MdViewerWindow::WriteTabSession() {
  if (!session_owner_ || settings_ == nullptr) {
    return;
  }
  SyncTabSessionFields();
  settings_->SaveSession();
}

void MdViewerWindow::SaveTabSession() {
  if (!session_owner_ || restoring_session_ || settings_ == nullptr) {
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
  if (tab == CurrentTab()) {
    DetachPanelsFromTab(tab);
  }
  if (tab->page != nullptr) {
    pages_->removeWidget(tab->page);
    // 先销毁页面（连带视图与信号），再销毁 DocumentTab
    delete tab->page;
    tab->page = nullptr;
    tab->view = nullptr;
  }
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
  tab_bar_->setTabText(index, welcome ? QStringLiteral("欢迎")
                                      : QFileInfo(tab->path).fileName());
  tab_bar_->setTabToolTip(index,
                          welcome ? QStringLiteral("欢迎页")
                                  : DocumentDisplayTitle(root_dir_, tab->path));
}

void MdViewerWindow::UpdateShellForActiveTab() {
  DocumentTab* tab = CurrentTab();
  if (close_tab_action_ != nullptr) {
    close_tab_action_->setEnabled(active_tab_ > 0);
  }
  if (tab_bar_ != nullptr) {
    const QSignalBlocker blocker(tab_bar_);
    if (active_tab_ < tab_bar_->count()) {
      tab_bar_->setCurrentIndex(active_tab_);
    }
    UpdateTabTitle(tab);
  }
  const bool welcome = tab == nullptr || tab->path.isEmpty();
  if (welcome) {
    setWindowTitle(QStringLiteral("Markdown 查看器"));
    if (welcome_ != nullptr) {
      welcome_->SetRecentFiles(settings_->recent_files, root_dir_);
    }
    if (path_label_ != nullptr) {
      path_label_->clear();
      path_label_->setToolTip(QString());
    }
    if (documents_panel_ != nullptr) {
      documents_panel_->SetCurrentFile(QString());
    }
    if (outline_dock_ != nullptr && welcome_outline_was_visible_) {
      outline_dock_->hide();
    }
  } else {
    setWindowTitle(DocumentDisplayTitle(root_dir_, tab->path));
    ReaderPage* page = tab->page;
    if (page != nullptr) {
      page->breadcrumb()->setText(DocumentDisplayTitle(root_dir_, tab->path));
      page->breadcrumb()->show();
    }
    const QString status_path = QDir::toNativeSeparators(tab->path);
    if (path_label_ != nullptr) {
      path_label_->setText(QFontMetrics(path_label_->font())
                               .elidedText(status_path, Qt::ElideMiddle, 520));
      path_label_->setToolTip(status_path);
    }
    if (documents_panel_ != nullptr) {
      documents_panel_->SetCurrentFile(tab->path);
    }
    if (welcome_outline_was_visible_ && outline_dock_ != nullptr) {
      outline_dock_->show();
    }
  }
  UpdateBookmarkUi();
  UpdateHistoryActions();
  UpdateReadingTime();
  if (CurrentView() != nullptr) {
    UpdateReadingProgress(CurrentView()->ScrollPosition());
  }
}

void MdViewerWindow::createActions() {
  open_file_action_ = new QAction(QStringLiteral("打开文件…"), this);
  open_file_action_->setShortcut(QKeySequence::Open);
  open_dir_action_ = new QAction(QStringLiteral("打开目录…"), this);
  new_tab_action_ = new QAction(QStringLiteral("新建标签页"), this);
  new_tab_action_->setObjectName(QStringLiteral("newTabAction"));
  new_tab_action_->setShortcut(QKeySequence(QStringLiteral("Ctrl+T")));
  close_tab_action_ = new QAction(QStringLiteral("关闭标签页"), this);
  close_tab_action_->setObjectName(QStringLiteral("closeTabAction"));
  close_tab_action_->setShortcut(QKeySequence(QStringLiteral("Ctrl+W")));
  new_window_action_ = new QAction(QStringLiteral("新建窗口"), this);
  new_window_action_->setObjectName(QStringLiteral("newWindowAction"));
  new_window_action_->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+N")));
  unload_workspace_action_ = new QAction(QStringLiteral("卸载工作区"), this);
  unload_workspace_action_->setObjectName(
      QStringLiteral("unloadWorkspaceAction"));
  unload_workspace_action_->setEnabled(false);
  quit_action_ = new QAction(QStringLiteral("退出"), this);
  quit_action_->setShortcut(QKeySequence::Quit);
  about_action_ = new QAction(QStringLiteral("关于"), this);
  export_html_action_ = new QAction(QStringLiteral("导出 HTML…"), this);
  export_html_action_->setObjectName(QStringLiteral("exportHtmlAction"));
  export_pdf_action_ = new QAction(QStringLiteral("导出 PDF…"), this);

  // 全局搜索入口：Ctrl+F 沿用肌肉记忆，Ctrl+K 与原型一致
  find_action_ = new QAction(QStringLiteral("搜索文档与正文"), this);
  find_action_->setShortcuts(
      QList<QKeySequence>{QKeySequence(QKeySequence::Find),
                          QKeySequence(QStringLiteral("Ctrl+K"))});
  find_action_->setToolTip(QStringLiteral("搜索文件名与正文（Ctrl+K）"));
}

void MdViewerWindow::initUi() {
  setObjectName(QStringLiteral("mdViewerWindow"));
  setWindowTitle(QStringLiteral("Markdown 查看器"));
  resize(1100, 800);

  // 中心区：欢迎页固定第 0 页，文档标签各占一页
  pages_ = new QStackedWidget(this);
  pages_->setObjectName(QStringLiteral("readerContainer"));
  pages_->installEventFilter(this);
  welcome_ = new WelcomeView(pages_);
  welcome_->setObjectName(QStringLiteral("welcomeView"));
  pages_->addWidget(welcome_);
  setCentralWidget(pages_);

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

  // 递归文档树 / 书签 / 搜索结果的侧栏
  document_dock_ = new QDockWidget(QStringLiteral("文档"), this);
  document_dock_->setObjectName(QStringLiteral("documentDock"));
  document_dock_->setMinimumWidth(278);
  // 侧栏是窄栏；不设上限时搜索结果会把左栏撑到 400 以上
  document_dock_->setMaximumWidth(320);
  documents_panel_ = new DocumentsPanel(document_dock_);
  document_dock_->setWidget(documents_panel_);
  addDockWidget(Qt::LeftDockWidgetArea, document_dock_);

  // 大纲：标题层级 + 点击回跳；原型把它放在正文右侧整列
  outline_dock_ = new QDockWidget(QStringLiteral("大纲"), this);
  outline_dock_->setObjectName(QStringLiteral("outlineDock"));
  outline_ = new MarkdownOutlineWidget(outline_dock_);
  outline_dock_->setWidget(outline_);
  addDockWidget(Qt::RightDockWidgetArea, outline_dock_);

  // 渲染状态：引擎加载情况与渲染/命中计数，独立成 dock 便于常开观察
  auto* status_dock = new QDockWidget(QStringLiteral("渲染状态"), this);
  status_dock->setObjectName(QStringLiteral("renderStatusDock"));
  render_status_ = new RenderStatusPanel(CurrentView(), status_dock);
  status_dock->setWidget(render_status_);
  addDockWidget(Qt::RightDockWidgetArea, status_dock);
  // 默认收起；需要时从“视图”菜单打开。已保存的 dock 布局会覆盖这个默认值
  status_dock->hide();

  // 渲染设置：模型对话框，改动即时生效；关闭只是隐藏
  render_settings_ = new RenderSettingsPanel(CurrentView(), this);
  render_settings_->SetWindowMaterial(settings_->window_material);
  render_settings_->SetReadingStyle(settings_->reading_style);
  render_settings_->SetTransparentReading(settings_->transparent_reading);
  settings_dialog_ = new SettingsDialog(render_settings_, this);

  statusBar()->setObjectName(QStringLiteral("mainStatusBar"));
  statusBar()->setSizeGripEnabled(false);
  statusBar()->setFixedHeight(30);

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
  status_ready_label_ = new QLabel(QStringLiteral("就绪"), ready_widget);
  status_ready_label_->setObjectName(QStringLiteral("statusReadyLabel"));
  ready_layout->addWidget(status_ready_label_);
  left_layout->addWidget(ready_widget);

  path_label_ = new QLabel(left_widget);
  path_label_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
  path_label_->setMinimumWidth(0);
  left_layout->addWidget(path_label_, 1);
  statusBar()->addWidget(left_widget, 1);

  render_progress_ = new QProgressBar(this);
  render_progress_->setObjectName(QStringLiteral("renderProgressBar"));
  render_progress_->setTextVisible(true);
  render_progress_->setFormat(QStringLiteral("渲染 %v/%m"));
  render_progress_->setFixedWidth(160);
  render_progress_->setToolTip(QStringLiteral("当前文档的渲染任务进度"));
  render_progress_->hide();
  document_count_label_ = new QLabel(QStringLiteral("0 篇"), this);
  document_count_label_->setObjectName(QStringLiteral("documentCountLabel"));
  document_count_label_->setToolTip(
      QStringLiteral("当前目录中的 Markdown 文档数"));
  reading_time_label_ = new QLabel(this);
  reading_time_label_->setObjectName(QStringLiteral("readingTimeLabel"));
  reading_time_label_->setToolTip(
      QStringLiteral("按源字符数估算，约 500 字/分钟"));
  backend_label_ =
      new QLabel(QStringLiteral(QLITEMD_BACKEND_LABEL)
                     .replace(QStringLiteral(" / "), QStringLiteral(" · ")),
                 this);
  backend_label_->setObjectName(QStringLiteral("backendLabel"));
  backend_label_->setToolTip(QStringLiteral("当前构建的渲染后端"));
  zoom_label_ = new QLabel(this);
  zoom_label_->setObjectName(QStringLiteral("zoomLabel"));
  zoom_label_->setToolTip(
      QStringLiteral("Ctrl+滚轮 / Ctrl+= / Ctrl+- / Ctrl+0"));

  auto* stats_widget = new QWidget(this);
  stats_widget->setObjectName(QStringLiteral("statusStatsWidget"));
  auto* stats_layout = new QHBoxLayout(stats_widget);
  stats_layout->setContentsMargins(0, 0, 0, 0);
  stats_layout->setSpacing(14);
  stats_layout->addWidget(document_count_label_);
  stats_layout->addWidget(reading_time_label_);
  stats_layout->addWidget(backend_label_);
  stats_layout->addWidget(zoom_label_);
  statusBar()->addPermanentWidget(render_progress_);
  statusBar()->addPermanentWidget(stats_widget);
  UpdateZoomLabel(CurrentView()->zoom());

  // 无边框壳 + Fluent 标题栏；菜单栏不再单独占一行
  fluent_frame_ = new FluentWindowFrame(this, this);
  fluent_frame_->installChromeHeader(nullptr);
  // 强调色按钮只在标题栏内部改选中态并发信号，查看器没有消费方，隐藏
  for (QToolButton* button : fluent_frame_->titleBar()->accentButtons()) {
    button->setVisible(false);
  }
  // 与文档主题初始一致；标题栏据此选搜索图标等自身角色的配色
  fluent_frame_->titleBar()->setThemeDark(CurrentView()->theme() !=
                                          MarkdownTheme::Light);

  // 快速跳转：统一入口，Ctrl+P 与 VS Code 的快速打开同键
  const bool initial_dark = settings_->dark_theme;
  quick_open_action_ = new QAction(QStringLiteral("快速跳转…"), this);
  quick_open_action_->setObjectName(QStringLiteral("quickOpenAction"));
  quick_open_action_->setIcon(
      ThemeIcon(QStringLiteral("search"), initial_dark));
  quick_open_action_->setShortcut(QKeySequence(QStringLiteral("Ctrl+P")));
  quick_open_action_->setToolTip(
      QStringLiteral("跳转到文件、标题或命令（Ctrl+P）"));

  // 导航与缩放从工具条搬进标题栏；文件跳转和锚点跳转共用一个历史栈
  back_action_ = new QAction(QStringLiteral("后退"), this);
  back_action_->setObjectName(QStringLiteral("navBackAction"));
  back_action_->setIcon(ThemeIcon(QStringLiteral("back"), initial_dark));
  back_action_->setShortcut(QKeySequence(QStringLiteral("Ctrl+Alt+Left")));
  back_action_->setToolTip(QStringLiteral("后退（Ctrl+Alt+←）"));

  forward_action_ = new QAction(QStringLiteral("前进"), this);
  forward_action_->setObjectName(QStringLiteral("navForwardAction"));
  forward_action_->setIcon(ThemeIcon(QStringLiteral("forward"), initial_dark));
  forward_action_->setShortcut(QKeySequence(QStringLiteral("Ctrl+Alt+Right")));
  forward_action_->setToolTip(QStringLiteral("前进（Ctrl+Alt+→）"));

  // 缩放快捷键由 MarkdownView 内部持有，这里的按钮不再重复注册快捷键
  zoom_out_action_ = new QAction(QStringLiteral("−"), this);
  zoom_out_action_->setObjectName(QStringLiteral("zoomOutAction"));
  zoom_out_action_->setIcon(
      ThemeIcon(QStringLiteral("zoom-out"), initial_dark));
  zoom_out_action_->setToolTip(QStringLiteral("缩小（Ctrl+-）"));

  zoom_reset_action_ = new QAction(QStringLiteral("100%"), this);
  zoom_reset_action_->setObjectName(QStringLiteral("zoomResetAction"));
  zoom_reset_action_->setIcon(
      ThemeIcon(QStringLiteral("zoom-reset"), initial_dark));
  zoom_reset_action_->setToolTip(QStringLiteral("重置缩放（Ctrl+0）"));

  settings_action_ = new QAction(QStringLiteral("设置…"), this);
  settings_action_->setObjectName(QStringLiteral("settingsAction"));
  settings_action_->setShortcut(QKeySequence(QStringLiteral("Ctrl+,")));
  settings_action_->setToolTip(QStringLiteral("设置（Ctrl+,）"));

  zoom_in_action_ = new QAction(QStringLiteral("+"), this);
  zoom_in_action_->setObjectName(QStringLiteral("zoomInAction"));
  zoom_in_action_->setIcon(ThemeIcon(QStringLiteral("zoom-in"), initial_dark));
  zoom_in_action_->setToolTip(QStringLiteral("放大（Ctrl+=）"));

  bookmark_action_ = new QAction(QStringLiteral("收藏当前文档"), this);
  bookmark_action_->setObjectName(QStringLiteral("bookmarkAction"));
  bookmark_action_->setShortcut(QKeySequence(QStringLiteral("Ctrl+D")));
  bookmark_action_->setToolTip(QStringLiteral("收藏当前文档（Ctrl+D）"));
  UpdateHistoryActions();

  // 汉堡菜单在 dock 建好后创建，视图菜单才能挂上 toggleViewAction
  app_menu_ = new QMenu(this);
  QMenu* file_menu = app_menu_->addMenu(QStringLiteral("文件(&F)"));
  file_menu->addAction(new_tab_action_);
  file_menu->addAction(close_tab_action_);
  file_menu->addAction(new_window_action_);
  file_menu->addSeparator();
  file_menu->addAction(open_file_action_);
  file_menu->addAction(open_dir_action_);
  file_menu->addAction(unload_workspace_action_);
  recent_menu_ = file_menu->addMenu(QStringLiteral("最近打开"));
  recent_roots_menu_ = file_menu->addMenu(QStringLiteral("最近打开的工作区"));
  file_menu->addSeparator();
  file_menu->addAction(export_html_action_);
  file_menu->addAction(export_pdf_action_);
  file_menu->addSeparator();
  file_menu->addAction(quit_action_);

  view_menu_ = app_menu_->addMenu(QStringLiteral("视图(&V)"));
  view_menu_->addAction(quick_open_action_);
  view_menu_->addSeparator();
  view_menu_->addAction(document_dock_->toggleViewAction());
  view_menu_->addAction(outline_dock_->toggleViewAction());
  view_menu_->addAction(status_dock->toggleViewAction());
  view_menu_->addSeparator();
  view_menu_->addAction(back_action_);
  view_menu_->addAction(forward_action_);
  refresh_action_ = new QAction(QStringLiteral("刷新"), this);
  refresh_action_->setObjectName(QStringLiteral("refreshAction"));
  refresh_action_->setShortcut(QKeySequence(Qt::Key_F5));
  view_menu_->addAction(refresh_action_);
  view_menu_->addSeparator();
  view_menu_->addAction(zoom_out_action_);
  view_menu_->addAction(zoom_reset_action_);
  view_menu_->addAction(zoom_in_action_);
  view_menu_->addSeparator();
  view_menu_->addAction(settings_action_);
#ifdef Q_OS_WIN
  QMenu* material_menu = view_menu_->addMenu(QStringLiteral("窗口材质"));
  auto* material_group = new QActionGroup(this);
  material_auto_action_ =
      material_menu->addAction(QStringLiteral("自动（Mica）"));
  material_auto_action_->setObjectName(QStringLiteral("materialAutoAction"));
  material_acrylic_action_ = material_menu->addAction(QStringLiteral("亚克力"));
  material_acrylic_action_->setObjectName(
      QStringLiteral("materialAcrylicAction"));
  material_none_action_ = material_menu->addAction(QStringLiteral("关闭"));
  material_none_action_->setObjectName(QStringLiteral("materialNoneAction"));
  for (QAction* action : {material_auto_action_, material_acrylic_action_,
                          material_none_action_}) {
    action->setCheckable(true);
    material_group->addAction(action);
  }
  connect(material_auto_action_, &QAction::triggered, this,
          [this] { ApplyWindowMaterial(WindowMaterial::kAuto, true); });
  connect(material_acrylic_action_, &QAction::triggered, this,
          [this] { ApplyWindowMaterial(WindowMaterial::kAcrylic, true); });
  connect(material_none_action_, &QAction::triggered, this,
          [this] { ApplyWindowMaterial(WindowMaterial::kNone, true); });
  UpdateMaterialActions();
#endif

  QMenu* help_menu = app_menu_->addMenu(QStringLiteral("帮助(&H)"));
  help_menu->addAction(about_action_);

  // 菜单挂到标题栏按钮后不再是 QMenuBar；关键动作仍挂到窗口，快捷键才稳
  addAction(open_file_action_);
  addAction(open_dir_action_);
  addAction(new_tab_action_);
  addAction(close_tab_action_);
  addAction(new_window_action_);
  addAction(quit_action_);
  addAction(find_action_);
  addAction(quick_open_action_);
  addAction(back_action_);
  addAction(forward_action_);
  addAction(bookmark_action_);
  addAction(refresh_action_);

  menu_button_ = new QToolButton(fluent_frame_->titleBar());
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
  menu_button_->setToolTip(QStringLiteral("菜单"));

  // 标题栏左侧：菜单 + 标签条 + 新建；标签条吃掉剩余宽度
  title_leading_widget_ = new QWidget(fluent_frame_->titleBar());
  title_leading_widget_->setObjectName(QStringLiteral("titleLeadingWidget"));
  auto* leading_layout = new QHBoxLayout(title_leading_widget_);
  leading_layout->setContentsMargins(0, 0, 0, 0);
  leading_layout->setSpacing(2);
  leading_layout->addWidget(menu_button_);
  tab_bar_ = new DocumentTabBar(title_leading_widget_);
  tab_bar_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  leading_layout->addWidget(tab_bar_, 1);
  add_tab_button_ = new QToolButton(title_leading_widget_);
  add_tab_button_->setObjectName(QStringLiteral("addTabButton"));
  add_tab_button_->setAutoRaise(true);
  add_tab_button_->setText(QStringLiteral("+"));
  add_tab_button_->setToolTip(QStringLiteral("新建标签页（Ctrl+T）"));
  leading_layout->addWidget(add_tab_button_);
  fluent_frame_->titleBar()->setLeadingWidget(title_leading_widget_);
  // 标题由标签显示，标题栏不再单独占一行文字；把剩余宽度让给标签条
  fluent_frame_->titleBar()->setTitleVisible(false);
  if (auto* title_layout =
          qobject_cast<QHBoxLayout*>(fluent_frame_->titleBar()->layout())) {
    title_layout->setStretchFactor(title_leading_widget_, 1);
  }
  // 欢迎标签固定第 0 位、不可关闭
  tab_bar_->addTab(QStringLiteral("欢迎"));
  tab_bar_->setTabToolTip(0, QStringLiteral("欢迎页"));
  tab_bar_->RemoveCloseButton(0);

  nav_widget_ = new QWidget(fluent_frame_->titleBar());
  nav_widget_->setObjectName(QStringLiteral("navWidget"));
  auto* nav_layout = new QHBoxLayout(nav_widget_);
  nav_layout->setContentsMargins(0, 0, 0, 0);
  nav_layout->setSpacing(2);

  auto add_nav_button = [&](QAction* action, const QString& object_name,
                            Qt::ToolButtonStyle button_style) {
    auto* button = new QToolButton(nav_widget_);
    button->setObjectName(object_name);
    button->setAutoRaise(true);
    button->setDefaultAction(action);
    button->setToolButtonStyle(button_style);
    nav_layout->addWidget(button);
    return button;
  };
  add_nav_button(back_action_, QStringLiteral("navBack"),
                 Qt::ToolButtonIconOnly);
  add_nav_button(forward_action_, QStringLiteral("navForward"),
                 Qt::ToolButtonIconOnly);
  star_button_ = new QToolButton(nav_widget_);
  star_button_->setObjectName(QStringLiteral("starButton"));
  star_button_->setAutoRaise(true);
  star_button_->setCheckable(true);
  star_button_->setToolButtonStyle(Qt::ToolButtonIconOnly);
  star_button_->setIcon(
      ThemeIcon(QStringLiteral("star-outline"), settings_->dark_theme));
  star_button_->setToolTip(QStringLiteral("收藏当前文档（Ctrl+D）"));
  nav_layout->addWidget(star_button_);

  // 阅读外观：不做成 dock，避免打断阅读；弹出面板直接改当前文档
  appearance_popup_ = new ReaderAppearancePopup(this);
  connect(appearance_popup_, &ReaderAppearancePopup::preferencesChanged, this,
          [this](const ReaderPreferences& preferences) {
            settings_->reading_font_size = preferences.font_size;
            settings_->reading_line_height_percent =
                preferences.line_height_percent;
            settings_->reading_measure_px = preferences.measure_px;
            settings_->Save();
            ApplyReaderAppearance();
          });
  appearance_button_ = new QToolButton(nav_widget_);
  appearance_button_->setObjectName(QStringLiteral("appearanceButton"));
  appearance_button_->setAutoRaise(true);
  appearance_button_->setIcon(
      ThemeIcon(QStringLiteral("appearance"), settings_->dark_theme));
  appearance_button_->setToolButtonStyle(Qt::ToolButtonIconOnly);
  appearance_button_->setToolTip(
      QStringLiteral("阅读外观（字号、行距、栏宽）"));
  nav_layout->addWidget(appearance_button_);
  connect(appearance_button_, &QToolButton::clicked, this,
          &MdViewerWindow::ShowReaderAppearance);

  zoom_out_button_ = add_nav_button(zoom_out_action_, QStringLiteral("zoomOut"),
                                    Qt::ToolButtonIconOnly);
  zoom_reset_button_ = add_nav_button(
      zoom_reset_action_, QStringLiteral("zoomReset"), Qt::ToolButtonIconOnly);
  zoom_in_button_ = add_nav_button(zoom_in_action_, QStringLiteral("zoomIn"),
                                   Qt::ToolButtonIconOnly);
  settings_button_ = new QToolButton(nav_widget_);
  settings_button_->setObjectName(QStringLiteral("settingsButton"));
  settings_button_->setAutoRaise(true);
  settings_button_->setIcon(
      ThemeIcon(QStringLiteral("settings"), settings_->dark_theme));
  settings_button_->setToolButtonStyle(Qt::ToolButtonIconOnly);
  settings_button_->setToolTip(QStringLiteral("设置（Ctrl+,）"));
  nav_layout->addWidget(settings_button_);
  fluent_frame_->titleBar()->setTrailingWidget(nav_widget_);

  // 标题栏搜索框现在是全局搜索的唯一入口
  search_edit_ = fluent_frame_->titleBar()->searchLineEdit();
  search_edit_->setObjectName(QStringLiteral("titleSearchEdit"));
  search_edit_->setPlaceholderText(QStringLiteral("搜索文档与正文（Ctrl+K）"));
  search_timer_ = new QTimer(this);
  search_timer_->setSingleShot(true);
  search_timer_->setInterval(300);
  search_ = new TextSearch(this);
  root_watcher_ = new QFileSystemWatcher(this);
  root_rescan_timer_ = new QTimer(this);
  root_rescan_timer_->setSingleShot(true);
  root_rescan_timer_->setInterval(300);

  RefreshRecentRootsMenu();
  ApplyWindowMaterial(settings_->window_material, /*announce=*/false);
  ApplyReadingStyle();
  UpdateChromeVisibility();
  UpdateBookmarkUi();
  RefreshRecentMenu();
}

void MdViewerWindow::initSignals() {
  // 标题栏自带的主题按钮：Gallery 同款，点了就整窗（壳 + 文档）换肤
  connect(fluent_frame_->titleBar()->themeButton(), &QToolButton::clicked, this,
          &MdViewerWindow::ToggleTheme);
  connect(fluent_frame_->titleBar()->pinButton(), &QToolButton::toggled, this,
          [this](bool checked) { SetTopMost(this, checked); });
  connect(render_settings_, &RenderSettingsPanel::themeChanged, this,
          &MdViewerWindow::ApplyTheme);
  connect(render_settings_, &RenderSettingsPanel::windowMaterialChanged, this,
          [this](WindowMaterial material) {
            ApplyWindowMaterial(material, /*announce=*/true);
          });
  connect(render_settings_, &RenderSettingsPanel::readingStyleChanged, this,
          [this](ReadingStyle style) {
            settings_->reading_style = style;
            settings_->Save();
            ApplyReadingStyle();
            statusBar()->showMessage(style == ReadingStyle::kPrototype
                                         ? QStringLiteral("阅读样式：原型")
                                         : QStringLiteral("阅读样式：库默认"),
                                     2000);
          });
  connect(render_settings_, &RenderSettingsPanel::transparentReadingChanged,
          this, [this](bool enabled) {
            settings_->transparent_reading = enabled;
            settings_->Save();
            UpdateReadingSurface();
            statusBar()->showMessage(enabled
                                         ? QStringLiteral("阅读区：透明")
                                         : QStringLiteral("阅读区：主题底色"),
                                     2000);
          });
  connect(back_action_, &QAction::triggered, this, &MdViewerWindow::GoBack);
  connect(forward_action_, &QAction::triggered, this,
          &MdViewerWindow::GoForward);
  connect(zoom_in_action_, &QAction::triggered, this, [this] {
    if (CurrentView() != nullptr) {
      CurrentView()->ZoomIn();
    }
  });
  connect(zoom_out_action_, &QAction::triggered, this, [this] {
    if (CurrentView() != nullptr) {
      CurrentView()->ZoomOut();
    }
  });
  connect(zoom_reset_action_, &QAction::triggered, this, [this] {
    if (CurrentView() != nullptr) {
      CurrentView()->ResetZoom();
    }
  });
  connect(open_file_action_, &QAction::triggered, this,
          &MdViewerWindow::OpenFileDialog);
  connect(open_dir_action_, &QAction::triggered, this,
          &MdViewerWindow::OpenDirectoryDialog);
  connect(unload_workspace_action_, &QAction::triggered, this,
          &MdViewerWindow::UnloadWorkspace);
  connect(quit_action_, &QAction::triggered, this, &QWidget::close);
  connect(about_action_, &QAction::triggered, this, [this] {
    QMessageBox::about(
        this, QStringLiteral("关于"),
        QStringLiteral("qt_md_viewer——qlitemd 的 Markdown "
                       "查看器应用。\n\n"
                       "界面字体：MiSans（小米 MiSans 字体许可）\n\n"
                       "作者：zhouyonghua\n"
                       "邮箱：zhouyohu@163.com"));
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
  connect(documents_panel_, &DocumentsPanel::fileActivated, this,
          [this](const QString& path, bool new_tab) {
            NavigateTo(path, QString(), /*record_history=*/true,
                       new_tab ? OpenIntent::kNewTab : OpenIntent::kReplace);
          });
  connect(documents_panel_, &DocumentsPanel::chooseRootRequested, this,
          &MdViewerWindow::OpenDirectoryDialog);
  connect(documents_panel_, &DocumentsPanel::bookmarkActivated, this,
          &MdViewerWindow::OpenBookmark);
  connect(documents_panel_, &DocumentsPanel::bookmarkToggleRequested, this,
          &MdViewerWindow::ToggleBookmarkForPath);
  connect(documents_panel_, &DocumentsPanel::bookmarkRemoveRequested, this,
          [this](const QString& path) {
            bookmarks_.Remove(path);
            settings_->bookmarks_json = bookmarks_.ToJson();
            settings_->SaveBookmarks();
            UpdateBookmarkUi();
          });
  connect(star_button_, &QToolButton::clicked, this,
          &MdViewerWindow::SetBookmarkChecked);
  connect(bookmark_action_, &QAction::triggered, this, [this] {
    const QString path = CurrentTab()->path;
    SetBookmarkChecked(!path.isEmpty() && !bookmarks_.Contains(path));
  });
  connect(outline_, &MarkdownOutlineWidget::headingActivated, this,
          [this](const QString& anchor) {
            NavigateTo(CurrentView()->current_file(), anchor,
                       /*record_history=*/true);
          });
  connect(render_settings_, &RenderSettingsPanel::statusMessage, this,
          [this](const QString& message) {
            statusBar()->showMessage(message, 5000);
          });
  connect(
      render_settings_, &RenderSettingsPanel::settingsChanged, this, [this] {
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
  connect(settings_action_, &QAction::triggered, this,
          &MdViewerWindow::ShowSettings);
  connect(settings_button_, &QToolButton::clicked, this,
          &MdViewerWindow::ShowSettings);
  connect(render_status_, &RenderStatusPanel::statusMessage, this,
          [this](const QString& message) {
            statusBar()->showMessage(message, 5000);
          });
  connect(export_html_action_, &QAction::triggered, this,
          &MdViewerWindow::ExportHtml);
  connect(export_pdf_action_, &QAction::triggered, this,
          &MdViewerWindow::ExportPdf);
  connect(quick_open_action_, &QAction::triggered, this,
          &MdViewerWindow::ShowQuickOpen);
  connect(find_action_, &QAction::triggered, this,
          &MdViewerWindow::FocusGlobalSearch);
  // 新输入立即取消旧扫描并清空结果；防抖结束后才真正开扫
  connect(search_edit_, &QLineEdit::textChanged, this,
          [this](const QString& text) {
            search_->Cancel();
            active_search_query_.reset();
            active_search_id_.reset();
            search_timer_->stop();
            if (text.size() < kSearchMinQueryLength) {
              documents_panel_->BeginSearch(QString());
              return;
            }
            documents_panel_->BeginSearch(text);
            documents_panel_->ShowTab(DocumentsPanel::Tab::kSearch);
            search_timer_->start();
          });
  connect(search_timer_, &QTimer::timeout, this, &MdViewerWindow::StartSearch);
  connect(search_edit_, &QLineEdit::returnPressed, this, [this] {
    search_timer_->stop();
    StartSearch();
  });
  connect(search_, &TextSearch::batchReady, this,
          [this](quint64 request_id, const QVector<SearchHit>& hits,
                 int scanned, int total) {
            if (!active_search_id_.has_value() ||
                request_id != *active_search_id_ ||
                !active_search_query_.has_value() ||
                search_edit_->text() != *active_search_query_) {
              return;
            }
            documents_panel_->AppendSearchHits(hits, scanned, total);
          });
  connect(search_, &TextSearch::finished, this,
          [this](quint64 request_id, bool truncated, bool canceled) {
            if (!active_search_id_.has_value() ||
                request_id != *active_search_id_ ||
                !active_search_query_.has_value() ||
                search_edit_->text() != *active_search_query_) {
              return;
            }
            documents_panel_->EndSearch(truncated, canceled);
          });
  connect(search_, &TextSearch::failed, this,
          [this](quint64 request_id, const QString& error) {
            if (!active_search_id_.has_value() ||
                request_id != *active_search_id_ ||
                !active_search_query_.has_value() ||
                search_edit_->text() != *active_search_query_) {
              return;
            }
            documents_panel_->SetSearchError(error);
          });
  connect(documents_panel_, &DocumentsPanel::searchHitActivated, this,
          &MdViewerWindow::OpenSearchHit);
  connect(documents_panel_, &DocumentsPanel::cancelSearchRequested, this,
          [this] { search_->Cancel(); });
  connect(root_watcher_, &QFileSystemWatcher::directoryChanged, this,
          [this](const QString&) { root_rescan_timer_->start(); });
  connect(root_watcher_, &QFileSystemWatcher::fileChanged, this,
          [this](const QString&) { root_rescan_timer_->start(); });
  connect(root_rescan_timer_, &QTimer::timeout, this,
          &MdViewerWindow::RefreshRoot);
  connect(refresh_action_, &QAction::triggered, this,
          &MdViewerWindow::RefreshRoot);
  // Esc 把焦点还给正文；搜索框常驻
  auto* escape = new QShortcut(QKeySequence(Qt::Key_Escape), this);
  connect(escape, &QShortcut::activated, this, [this] {
    if (search_edit_->hasFocus()) {
      CurrentView()->setFocus();
    }
  });
  connect(new_tab_action_, &QAction::triggered, this,
          &MdViewerWindow::OpenFileDialog);
  connect(new_window_action_, &QAction::triggered, this, [this] {
    // 次窗口不读写会话：停在自己的欢迎页，多工作区靠它实现
    auto* window = new MdViewerWindow(settings_, /*session_owner=*/false);
    window->setAttribute(Qt::WA_DeleteOnClose);
    window->resize(1100, 800);
    window->show();
  });
  connect(close_tab_action_, &QAction::triggered, this,
          [this] { CloseTab(active_tab_); });
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
    connect(tab_bar_, &DocumentTabBar::newTabRequested, this,
            &MdViewerWindow::OpenFileDialog);
    connect(tab_bar_, &QTabBar::tabMoved, this, &MdViewerWindow::OnTabMoved);
  }
  if (add_tab_button_ != nullptr) {
    connect(add_tab_button_, &QToolButton::clicked, this,
            &MdViewerWindow::OpenFileDialog);
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
  if (search_ != nullptr) {
    search_->Cancel();
    active_search_query_.reset();
    active_search_id_.reset();
    documents_panel_->BeginSearch(QString());
  }
  const DocumentScanResult result = ScanDocumentRoot(root);
  if (result.root_exists) {
    root_dir_ = result.root;
    settings_->root_dir = root_dir_;
    // 任何一次成功换根都视为"重新有了工作区"，解除卸载标志
    settings_->workspace_closed = false;
    settings_->AddRecentRoot(root_dir_);
    RefreshRecentRootsMenu();
    SaveTabSession();
    if (unload_workspace_action_ != nullptr) {
      unload_workspace_action_->setEnabled(true);
    }
    documents_panel_->SetRoot(result);
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
    documents_panel_->SetRootMissing(root_dir_,
                                     QStringLiteral("目录不存在或不可读"));
  }
  if (document_count_label_ != nullptr) {
    document_count_label_->setText(QStringLiteral("%1 篇").arg(
        result.root_exists ? result.markdown_file_count : 0));
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
          QStringLiteral("目录中没有 Markdown 文件：%1").arg(result.root),
          5000);
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
  if (back_action_ != nullptr) {
    back_action_->setEnabled(CurrentTab()->history.CanGoBack());
  }
  if (forward_action_ != nullptr) {
    forward_action_->setEnabled(CurrentTab()->history.CanGoForward());
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
  positions_.Update(path, CurrentView()->ScrollPosition(),
                    QDateTime::currentSecsSinceEpoch());
  settings_->positions_json = positions_.ToJson();
  settings_->SavePositions();
}

void MdViewerWindow::RestoreSession() {
  // 上次显式卸载了工作区：重启回欢迎页，最近列表照常显示，
  // 但不自动恢复任何文档
  if (settings_->workspace_closed) {
    documents_panel_->SetRootMissing(
        QString(), QStringLiteral("从“文件 > 打开目录”开始阅读"));
    ShowWelcome();
    return;
  }
  // 恢复期间不改会话：SetRootDir/ShowFile 内部的保存都跳过，
  // 恢复完统一写回一次
  const QByteArray saved_tabs = settings_->open_tabs_json;
  restoring_session_ = true;
  struct RestoreScope {
    bool* flag;
    ~RestoreScope() { *flag = false; }
  } restore_scope{&restoring_session_};
  if (!settings_->root_dir.isEmpty()) {
    const DocumentScanResult result = SetRootDir(settings_->root_dir);
    if (!result.root_exists) {
      ShowWelcome();
      return;  // SetRootDir 已显示空态
    }
  }
  // 多标签会话优先；缺失的文件跳过，不阻塞其它标签
  TabModel model;
  if (!saved_tabs.isEmpty() && model.Restore(saved_tabs) && model.count() > 1) {
    for (int i = 1; i < model.count(); ++i) {
      const QString path = model.path_at(i);
      if (!QFileInfo(path).isFile()) {
        continue;
      }
      if (ShowFile(path, OpenReason::kRestore, OpenIntent::kNewTab)) {
        PushHistory(path, QString());
      }
    }
    if (tabs_.size() > 1) {
      const QString active_path = model.path_at(model.active());
      const int index = active_path.isEmpty() ? 0 : FindTabIndex(active_path);
      if (index >= 0) {
        ActivateTab(index);
      }
      WriteTabSession();
      return;
    }
  }
  // 旧会话（没有 open_tabs）按 last_file 恢复
  if (!settings_->root_dir.isEmpty()) {
    if (QFileInfo(settings_->last_file).isFile()) {
      const QString absolute =
          QFileInfo(settings_->last_file).absoluteFilePath();
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
          QStringLiteral("已恢复上次打开：%1").arg(QFileInfo(path).fileName()),
          3000);
    }
    return;
  }
  documents_panel_->SetRootMissing(
      QString(), QStringLiteral("从“文件 > 打开目录”开始阅读"));
  ShowWelcome();
}

void MdViewerWindow::OpenFileDialog() {
  const QString start_dir = root_dir_.isEmpty() ? QDir::homePath() : root_dir_;
  const QString path = QFileDialog::getOpenFileName(
      this, QStringLiteral("打开 Markdown"), start_dir,
      QStringLiteral("Markdown 文档 (*.md *.markdown);;所有文件 (*)"));
  if (!path.isEmpty()) {
    OpenPath(path);
  }
}

void MdViewerWindow::OpenDirectoryDialog() {
  const QString dir = QFileDialog::getExistingDirectory(
      this, QStringLiteral("打开目录"),
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
  if (search_ != nullptr) {
    search_->Cancel();
  }
  active_search_query_.reset();
  active_search_id_.reset();
  documents_panel_->BeginSearch(QString());
  documents_panel_->SetCurrentFile(QString());
  documents_panel_->SetRootMissing(
      QString(), QStringLiteral("从“文件 > 打开目录”开始阅读"));

  root_dir_.clear();
  settings_->root_dir.clear();
  settings_->last_file.clear();
  // 意图标志：没有它，重启时 OpenLastFile 会从最近列表把工作区拉回来
  settings_->workspace_closed = true;
  SaveTabSession();

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
  path_label_->clear();
  path_label_->setToolTip(QString());
  document_count_label_->setText(QStringLiteral("0 篇"));
  UpdateBookmarkUi();
  unload_workspace_action_->setEnabled(false);
  statusBar()->showMessage(QStringLiteral("已卸载工作区：%1")
                               .arg(QFileInfo(unloaded_root).fileName()),
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
    item.subtitle = QStringLiteral("最近 · %1")
                        .arg(QDir::toNativeSeparators(info.absolutePath()));
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
  add_command(QStringLiteral("打开文件…"), QStringLiteral("文件"),
              QStringLiteral("open-file"));
  add_command(QStringLiteral("打开目录…"), QStringLiteral("文件"),
              QStringLiteral("open-dir"));
  if (!root_dir_.isEmpty()) {
    add_command(QStringLiteral("卸载工作区"), QStringLiteral("文件"),
                QStringLiteral("unload-workspace"));
  }
  add_command(CurrentView()->theme() == MarkdownTheme::Dark
                  ? QStringLiteral("切换到浅色主题")
                  : QStringLiteral("切换到深色主题"),
              QStringLiteral("外观"), QStringLiteral("toggle-theme"));
  add_command(QStringLiteral("阅读外观…"), QStringLiteral("外观"),
              QStringLiteral("appearance"));
  const bool starred =
      !CurrentTab()->path.isEmpty() && bookmarks_.Contains(CurrentTab()->path);
  add_command(starred ? QStringLiteral("取消收藏当前文档")
                      : QStringLiteral("收藏当前文档"),
              QStringLiteral("阅读"), QStringLiteral("toggle-bookmark"));
  add_command(QStringLiteral("导出 HTML…"), QStringLiteral("导出"),
              QStringLiteral("export-html"));
  add_command(QStringLiteral("导出 PDF…"), QStringLiteral("导出"),
              QStringLiteral("export-pdf"));
  add_command(QStringLiteral("刷新文档树"), QStringLiteral("视图"),
              QStringLiteral("refresh-root"));
  if (CurrentView()->has_document() && !CurrentView()->is_rendering() &&
      !CurrentView()->GeneratedImageUrls().isEmpty()) {
    add_command(QStringLiteral("预览公式/图表"), QStringLiteral("阅读"),
                QStringLiteral("preview-image"));
  }
  add_command(QStringLiteral("缩小"), QStringLiteral("缩放"),
              QStringLiteral("zoom-out"));
  add_command(QStringLiteral("重置缩放"), QStringLiteral("缩放"),
              QStringLiteral("zoom-reset"));
  add_command(QStringLiteral("放大"), QStringLiteral("缩放"),
              QStringLiteral("zoom-in"));
  add_command(QStringLiteral("打开渲染设置"), QStringLiteral("视图"),
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
    open_file_action_->trigger();
  } else if (id == QLatin1String("open-dir")) {
    open_dir_action_->trigger();
  } else if (id == QLatin1String("unload-workspace")) {
    unload_workspace_action_->trigger();
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
    bookmark_action_->trigger();
  } else if (id == QLatin1String("export-html")) {
    export_html_action_->trigger();
  } else if (id == QLatin1String("export-pdf")) {
    export_pdf_action_->trigger();
  } else if (id == QLatin1String("refresh-root")) {
    RefreshRoot();
  } else if (id == QLatin1String("zoom-out")) {
    zoom_out_action_->trigger();
  } else if (id == QLatin1String("zoom-reset")) {
    zoom_reset_action_->trigger();
  } else if (id == QLatin1String("zoom-in")) {
    zoom_in_action_->trigger();
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
  fluent_frame_->titleBar()->setThemeDark(dark);
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
    active->ShowPlaceholder(QStringLiteral("正在渲染…"));
  }
  // 面板是被动同步方，用 QSignalBlocker 防止再发一次 themeChanged
  render_settings_->SetTheme(theme);
  RefreshChromePalette();
  UpdateReadingSurface();
  // 星标图标按调色板绘制，换肤后要重画
  RefreshThemeIcons();
  statusBar()->showMessage(theme == MarkdownTheme::Dark
                               ? QStringLiteral("已切换主题：暗色")
                               : QStringLiteral("已切换主题：浅色"),
                           3000);
}

void MdViewerWindow::RefreshThemeIcons() {
  // 主题相关图标分 svg/light 与 svg/dark 两套，换肤时整体换一套；
  // 星标里实心星是语义图标，UpdateBookmarkUi 内部处理
  const bool dark = ThemeDark();
  quick_open_action_->setIcon(ThemeIcon(QStringLiteral("search"), dark));
  back_action_->setIcon(ThemeIcon(QStringLiteral("back"), dark));
  forward_action_->setIcon(ThemeIcon(QStringLiteral("forward"), dark));
  zoom_out_action_->setIcon(ThemeIcon(QStringLiteral("zoom-out"), dark));
  zoom_reset_action_->setIcon(ThemeIcon(QStringLiteral("zoom-reset"), dark));
  zoom_in_action_->setIcon(ThemeIcon(QStringLiteral("zoom-in"), dark));
  if (menu_button_ != nullptr) {
    menu_button_->setIcon(ThemeIcon(QStringLiteral("menu"), dark));
  }
  if (appearance_button_ != nullptr) {
    appearance_button_->setIcon(ThemeIcon(QStringLiteral("appearance"), dark));
  }
  if (settings_button_ != nullptr) {
    settings_button_->setIcon(ThemeIcon(QStringLiteral("settings"), dark));
  }
  UpdateBookmarkUi();
  if (documents_panel_ != nullptr) {
    documents_panel_->SetThemeDark(dark);
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
  if (render_settings_ != nullptr) {
    render_settings_->SetWindowMaterial(material);
  }
  UpdateMaterialActions();
#ifdef Q_OS_WIN
  // 只切 backdrop 不够：关掉材质后调色板若仍透明，窗口会露底。
  // _q_widget_mode 与样式要一起切
  qApp->setProperty("_q_widget_mode",
                    material == WindowMaterial::kNone ? 0 : 2);
  qApp->setStyle(new FluentUI3Style);
  qApp->setPalette(qApp->style()->standardPalette());
  fluent_frame_->clearWindowBackdrop();
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
    if (!fluent_frame_->setWindowBackdrop(key)) {
      fluent_frame_->setWindowBackdrop(QStringLiteral("dwm-blur"));
    }
  }
  if (announce) {
    const QString message = material == WindowMaterial::kAcrylic
                                ? QStringLiteral("窗口材质：亚克力")
                            : material == WindowMaterial::kNone
                                ? QStringLiteral("窗口材质：关闭")
                                : QStringLiteral("窗口材质：自动（Mica）");
    statusBar()->showMessage(message, 3000);
  }
#else
  Q_UNUSED(announce);
#endif
  RefreshChromePalette();
  UpdateReadingSurface();
}

void MdViewerWindow::UpdateMaterialActions() {
  if (material_auto_action_ == nullptr) {
    return;
  }
  material_auto_action_->setChecked(window_material_ == WindowMaterial::kAuto);
  material_acrylic_action_->setChecked(window_material_ ==
                                       WindowMaterial::kAcrylic);
  material_none_action_->setChecked(window_material_ == WindowMaterial::kNone);
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
  apply_palette(document_dock_, app_palette);
  apply_palette(outline_dock_, app_palette);
  for (const auto& tab : tabs_) {
    if (tab->page != nullptr) {
      tab->page->breadcrumb()->setPalette(app_palette);
      tab->page->breadcrumb()->setForegroundRole(QPalette::PlaceholderText);
    }
  }
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
  if (render_settings_ != nullptr) {
    render_settings_->SetView(CurrentView());
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
  appearance_popup_->OpenNear(appearance_button_);
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
  if (outline_dock_ != nullptr && welcome_outline_was_visible_) {
    outline_dock_->hide();
  }
}

void MdViewerWindow::CaptureWelcomeState() {
  welcome_outline_was_visible_ =
      outline_dock_ != nullptr && !outline_dock_->isHidden();
  if (welcome_ != nullptr) {
    welcome_dock_state_ = saveState();
  }
}

void MdViewerWindow::HideWelcome() {
  // 页栈已经保证欢迎页与正文互斥，这里只负责恢复大纲的可见性
  if (welcome_outline_was_visible_ && outline_dock_ != nullptr) {
    outline_dock_->show();
  }
}

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
  reading_time_label_->setText(QStringLiteral("约 %1 分钟").arg(minutes));
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
  return QMainWindow::eventFilter(watched, event);
}

void MdViewerWindow::UpdateChromeVisibility() {
  if (fluent_frame_ == nullptr || fluent_frame_->titleBar() == nullptr) {
    return;
  }
  const int window_width = width();
  const bool show_zoom = window_width >= 900;
  for (QToolButton* button :
       {zoom_out_button_, zoom_reset_button_, zoom_in_button_}) {
    if (button != nullptr) {
      button->setVisible(show_zoom);
    }
  }
  if (search_edit_ != nullptr) {
    search_edit_->setVisible(window_width >= 780);
  }
  if (outline_dock_ != nullptr) {
    if (window_width < 980) {
      if (!outline_dock_->isHidden()) {
        outline_dock_->hide();
        outline_auto_hidden_ = true;
      }
    } else if (outline_auto_hidden_) {
      outline_dock_->show();
      outline_auto_hidden_ = false;
    }
  }
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
        this, QStringLiteral("大文件"),
        QStringLiteral("该文档约 %1，渲染期间窗口可能无响应。仍要打开吗？")
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
  target->scroll_position = positions_.ScrollFor(absolute_path);
  target->pending_restore = target->scroll_position >= 0;
  if (!target->view->LoadFile(absolute_path)) {
    target->pending_restore = false;
    target->pending_restore_path.clear();
    // 视图里已经提交了错误页；欢迎页正显示时必须让位，否则用户
    // 只看得到 5 秒状态栏消息
    HideWelcome();
    return false;
  }
  HideWelcome();
  // 旧文档还在屏幕上，而新文档已在后台渲染；先给一个明确的等待页
  target->view->ShowPlaceholder(QStringLiteral("正在渲染…"));
  target->path = absolute_path;
  // 标题栏、面包屑、状态栏路径统一由当前标签刷新
  UpdateShellForActiveTab();
  // 打开任意文件都算重新有了工作区
  settings_->workspace_closed = false;
  settings_->last_file = absolute_path;
  SaveTabSession();
  // 最近记录立即落盘：即使随后异常退出，也不会丢这次打开
  settings_->AddRecentFile(absolute_path);
  RefreshRecentMenu();
  if (reason == OpenReason::kRestore) {
    statusBar()->showMessage(
        QStringLiteral("已恢复 %1（%2）")
            .arg(info.fileName(), QLocale().formattedDataSize(info.size())),
        3000);
  }
  return true;
}

void MdViewerWindow::UpdateBookmarkUi() {
  const bool starred =
      !CurrentTab()->path.isEmpty() && bookmarks_.Contains(CurrentTab()->path);
  if (star_button_ != nullptr) {
    const QSignalBlocker blocker(star_button_);
    star_button_->setChecked(starred);
    // 实心星是语义图标（金色）；空心星走主题套
    star_button_->setIcon(
        starred ? QIcon(QStringLiteral(":/resources/svg/star-filled.svg"))
                : ThemeIcon(QStringLiteral("star-outline"), ThemeDark()));
    star_button_->setToolTip(starred
                                 ? QStringLiteral("取消收藏（Ctrl+D）")
                                 : QStringLiteral("收藏当前文档（Ctrl+D）"));
  }
  if (documents_panel_ != nullptr) {
    documents_panel_->SetBookmarks(bookmarks_.items(), root_dir_);
  }
}

void MdViewerWindow::SetBookmarkChecked(bool starred) {
  const QString path = CurrentTab()->path;
  if (path.isEmpty()) {
    UpdateBookmarkUi();
    statusBar()->showMessage(QStringLiteral("先打开一篇文档再收藏"), 3000);
    return;
  }
  if (bookmarks_.Contains(path) != starred) {
    bookmarks_.Toggle(path, root_dir_);
  }
  settings_->bookmarks_json = bookmarks_.ToJson();
  settings_->SaveBookmarks();
  UpdateBookmarkUi();
  statusBar()->showMessage(
      starred ? QStringLiteral("已收藏") : QStringLiteral("已取消收藏"), 2000);
}

void MdViewerWindow::ToggleBookmarkForPath(const QString& path) {
  if (path.isEmpty()) {
    return;
  }
  const bool starred = !bookmarks_.Contains(path);
  bookmarks_.Toggle(path, root_dir_);
  settings_->bookmarks_json = bookmarks_.ToJson();
  settings_->SaveBookmarks();
  UpdateBookmarkUi();
  statusBar()->showMessage(
      starred ? QStringLiteral("已收藏") : QStringLiteral("已取消收藏"), 2000);
}

void MdViewerWindow::OpenBookmark(const QString& path) {
  if (!QFileInfo(path).isFile()) {
    statusBar()->showMessage(
        QStringLiteral("文件已不存在：%1").arg(QFileInfo(path).fileName()),
        4000);
    return;
  }
  const bool outside_root = !IsPathUnderRoot(path, root_dir_);
  const QString absolute = QFileInfo(path).absoluteFilePath();
  if (!ShowFile(absolute, OpenReason::kUser, OpenIntent::kNewTab)) {
    return;
  }
  PushHistory(absolute, QString());
  if (outside_root) {
    statusBar()->showMessage(QStringLiteral("该书签不在当前目录内"), 4000);
  }
}

void MdViewerWindow::RefreshRecentMenu() {
  if (recent_menu_ == nullptr) {
    return;
  }
  recent_menu_->clear();
  if (settings_->recent_files.isEmpty()) {
    QAction* empty = recent_menu_->addAction(QStringLiteral("（无）"));
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
  QAction* clear = recent_menu_->addAction(QStringLiteral("清除最近记录"));
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
    QAction* empty = recent_roots_menu_->addAction(QStringLiteral("（无）"));
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
  QAction* clear =
      recent_roots_menu_->addAction(QStringLiteral("清除最近工作区"));
  connect(clear, &QAction::triggered, this, [this] {
    settings_->recent_roots.clear();
    settings_->SaveRecentRoots();
    RefreshRecentRootsMenu();
  });
}

void MdViewerWindow::OpenRecentRoot(const QString& root) {
  if (!QFileInfo(root).isDir()) {
    statusBar()->showMessage(QStringLiteral("工作区不存在：%1").arg(root),
                             4000);
    return;
  }
  NavigateTo(root, QString(), /*record_history=*/true, OpenIntent::kReplace);
}

void MdViewerWindow::SaveSettings() {
  // 会话与几何只归主窗口；次窗口退出不写任何东西
  if (!session_owner_ || settings_ == nullptr || CurrentView() == nullptr) {
    return;
  }
  RememberCurrentPosition();
  SyncTabSessionFields();
  settings_->window_geometry = saveGeometry();
  const bool welcome_visible = welcome_ != nullptr && welcome_->isVisible();
  if (welcome_visible && !welcome_dock_state_.isEmpty()) {
    // 欢迎页会临时收起空大纲；保存欢迎页之前的布局，别把临时
    // 隐藏写进下次启动的 dock_state
    settings_->dock_state = welcome_dock_state_;
  } else {
    settings_->dock_state = saveState();
  }
  CaptureGlobalSettings(CurrentTab());
  settings_->window_material = window_material_;
  settings_->root_dir = root_dir_;
  settings_->Save();
}

void MdViewerWindow::UpdateZoomLabel(qreal factor) {
  zoom_label_->setText(QStringLiteral("%1%").arg(qRound(factor * 100.0)));
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
              QStringLiteral("高亮"));
  note_engine(last_render_status_.formula, now.formula, QStringLiteral("公式"));
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
    statusBar()->showMessage(
        QStringLiteral("渲染引擎加载失败：%1")
            .arg(failed_engines.join(QStringLiteral("、"))),
        5000);
    return;
  }
  if (highlight_failures + formula_failures + mermaid_failures > 0) {
    QStringList parts;
    if (highlight_failures > 0) {
      parts << QStringLiteral("高亮 %1").arg(highlight_failures);
    }
    if (formula_failures > 0) {
      parts << QStringLiteral("公式 %1").arg(formula_failures);
    }
    if (mermaid_failures > 0) {
      parts << QStringLiteral("Mermaid %1").arg(mermaid_failures);
    }
    statusBar()->showMessage(QStringLiteral("本次渲染失败：%1")
                                 .arg(parts.join(QStringLiteral("、"))),
                             5000);
    return;
  }
  if (!loaded_engines.isEmpty()) {
    statusBar()->showMessage(
        QStringLiteral("渲染引擎已加载：%1")
            .arg(loaded_engines.join(QStringLiteral("、"))),
        3000);
  }
}

void MdViewerWindow::closeEvent(QCloseEvent* event) {
  SaveSettings();
  QMainWindow::closeEvent(event);
}

void MdViewerWindow::resizeEvent(QResizeEvent* event) {
  QMainWindow::resizeEvent(event);
  UpdateChromeVisibility();
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
    statusBar()->showMessage(QStringLiteral("已打开第一个拖入项：%1")
                                 .arg(QFileInfo(paths.first()).fileName()),
                             5000);
  }
}

void MdViewerWindow::FocusGlobalSearch() {
  // 窄宽度下搜索框会被隐藏；快捷键要能把它请回来
  if (!search_edit_->isVisible()) {
    search_edit_->setVisible(true);
  }
  search_edit_->setFocus();
  search_edit_->selectAll();
  documents_panel_->ShowTab(DocumentsPanel::Tab::kSearch);
}

void MdViewerWindow::StartSearch() {
  const QString query = search_edit_->text();
  if (query.size() < kSearchMinQueryLength) {
    return;
  }
  if (root_dir_.isEmpty() || !QFileInfo(root_dir_).isDir()) {
    documents_panel_->SetSearchError(QStringLiteral("先打开一个目录"));
    return;
  }
  active_search_query_ = query;
  documents_panel_->BeginSearch(query);
  active_search_id_ = search_->Start(root_dir_, query);
}

void MdViewerWindow::OpenSearchHit(const SearchHit& hit) {
  if (!QFileInfo(hit.file_path).isFile()) {
    statusBar()->showMessage(QStringLiteral("文件已不存在"), 4000);
    return;
  }
  const QString query = active_search_query_.has_value() ? *active_search_query_
                                                         : search_edit_->text();
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
      statusBar()->showMessage(
          QStringLiteral("无法精确定位该命中，已停在文档顶部"), 4000);
      return;
    }
  }
}

void MdViewerWindow::ExportHtml() {
  if (CurrentView()->is_rendering()) {
    statusBar()->showMessage(QStringLiteral("文档还在渲染，稍后再导出"), 3000);
    return;
  }
  if (!CurrentView()->has_document()) {
    statusBar()->showMessage(QStringLiteral("先打开一篇 Markdown 再导出"),
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
      this, QStringLiteral("导出 HTML"), default_path,
      QStringLiteral("HTML 文件 (*.html *.htm)"));
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
        error.isEmpty() ? QStringLiteral("导出 HTML 失败") : error, 5000);
    return;
  }
  statusBar()->showMessage(
      QStringLiteral("已导出：%1").arg(QFileInfo(path).fileName()), 5000);
}

void MdViewerWindow::OpenImage(const QUrl& url) { ShowImagePreview(url); }

void MdViewerWindow::ShowImageContextMenu(const QPoint& global_pos,
                                          const QUrl& url) {
  const QPixmap pixmap = CurrentView()->ImagePixmap(url);
  const std::optional<MarkdownImageInfo> info = CurrentView()->ImageInfo(url);

  QMenu menu(this);
  QAction* preview = menu.addAction(QStringLiteral("放大查看"));
  preview->setObjectName(QStringLiteral("imagePreviewAction"));
  preview->setEnabled(!pixmap.isNull());
  QAction* save = menu.addAction(QStringLiteral("图片另存为…"));
  save->setObjectName(QStringLiteral("imageSaveAsAction"));
  save->setEnabled(!pixmap.isNull());
  QAction* source = nullptr;
  if (info.has_value()) {
    menu.addSeparator();
    source = menu.addAction(QStringLiteral("查看源码"));
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
    statusBar()->showMessage(QStringLiteral("这张图片无法保存"), 3000);
    return;
  }
  QString start_dir = QDir::homePath();
  if (!CurrentView()->current_file().isEmpty()) {
    start_dir = QFileInfo(CurrentView()->current_file()).absolutePath();
  }
  QString path = QFileDialog::getSaveFileName(
      this, QStringLiteral("图片另存为"),
      start_dir + QStringLiteral("/image.png"),
      QStringLiteral("PNG 图片 (*.png);;JPEG 图片 (*.jpg *.jpeg);;"
                     "所有文件 (*)"));
  if (path.isEmpty()) {
    return;
  }
  if (QFileInfo(path).suffix().isEmpty()) {
    path += QStringLiteral(".png");
  }
  if (!pixmap.save(path)) {
    statusBar()->showMessage(QStringLiteral("图片保存失败"), 5000);
    return;
  }
  statusBar()->showMessage(
      QStringLiteral("已保存图片：%1").arg(QFileInfo(path).fileName()), 5000);
}

void MdViewerWindow::ShowImagePreview(const QUrl& url) {
  const QPixmap pixmap = CurrentView()->ImagePixmap(url);
  if (pixmap.isNull()) {
    statusBar()->showMessage(QStringLiteral("这张图片无法预览"), 3000);
    return;
  }
  const std::optional<MarkdownImageInfo> info = CurrentView()->ImageInfo(url);
  QString title = QStringLiteral("图片预览");
  if (info.has_value()) {
    title = info->kind == MarkdownImageKind::kFormula
                ? QStringLiteral("公式预览")
                : QStringLiteral("Mermaid 图表");
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
  QString title = QStringLiteral("Mermaid 源码");
  if (info->kind == MarkdownImageKind::kFormula) {
    title = QStringLiteral("公式源码");
    const QString delimiter =
        info->display ? QStringLiteral("$$") : QStringLiteral("$");
    source = delimiter + source + delimiter;
  }
  SourceDialog dialog(title, source, this);
  dialog.exec();
}

void MdViewerWindow::ExportPdf() {
  if (CurrentView()->is_rendering()) {
    statusBar()->showMessage(QStringLiteral("文档还在渲染，稍后再导出"), 3000);
    return;
  }
  if (!CurrentView()->has_document()) {
    statusBar()->showMessage(QStringLiteral("先打开一篇 Markdown 再导出"),
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
      this, QStringLiteral("导出 PDF"), default_path,
      QStringLiteral("PDF 文件 (*.pdf)"));
  if (path.isEmpty()) {
    return;  // 用户取消
  }
  if (!path.endsWith(QStringLiteral(".pdf"), Qt::CaseInsensitive)) {
    path += QStringLiteral(".pdf");
  }

  // 分页与 print 媒体重排都在控件里（见 MarkdownView::Print 的注释）
  if (CurrentView()->is_rendering()) {
    statusBar()->showMessage(QStringLiteral("文档还在渲染，稍后再导出"), 3000);
    return;
  }
  QPrinter printer(QPrinter::HighResolution);
  printer.setOutputFormat(QPrinter::PdfFormat);
  printer.setOutputFileName(path);
  CurrentView()->Print(&printer);

  statusBar()->showMessage(
      QStringLiteral("已导出：%1").arg(QFileInfo(path).fileName()), 5000);
}
