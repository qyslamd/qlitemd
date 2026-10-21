#include "md_viewer_window.h"

// Must precede windows.h: winuser.h defines FindText as FindTextA/W.
#include "markdown_view.h"

#include <fluenttitlebar.h>
#include <fluentui3style.h>
#include <fluentwindowframe.h>

#include <optional>

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QColor>
#include <QDesktopServices>
#include <QDialog>
#include <QDir>
#include <QDockWidget>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFileInfoList>
#include <QFont>
#include <QFontDatabase>
#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QPainter>
#include <QPalette>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPrinter>
#include <QProgressBar>
#include <QPushButton>
#include <QShortcut>
#include <QSize>
#include <QStatusBar>
#include <QStyle>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWheelEvent>

#ifdef Q_OS_WIN
#include <windows.h>
// winuser.h maps FindText to FindTextA/W; MarkdownView's method is not that.
#undef FindText
#endif

#include "markdown_outline_widget.h"
#include "md_viewer_settings.h"
#include "render_settings_panel.h"
#include "render_status_panel.h"

namespace {

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

// 与模块的 IsMarkdownFile 一个口径：目录里认这两种后缀
const QStringList& MarkdownFilters() {
  static const QStringList kFilters{QStringLiteral("*.md"),
                                    QStringLiteral("*.markdown")};
  return kFilters;
}

// 没有图标资源，就把字母/箭头画进 QPixmap：激活态用高亮色加粗
QIcon TextActionIcon(const QString& text, bool active) {
  constexpr int kIconSize = 18;
  QPixmap pixmap(kIconSize, kIconSize);
  pixmap.fill(Qt::transparent);
  QPainter painter(&pixmap);
  QFont font = QApplication::font();
  font.setPixelSize(12);
  font.setBold(active);
  painter.setFont(font);
  painter.setPen(active ? QApplication::palette().color(QPalette::Highlight)
                        : QApplication::palette().color(QPalette::WindowText));
  painter.drawText(pixmap.rect(), Qt::AlignCenter, text);
  return QIcon(pixmap);
}

class ImageView final : public QGraphicsView {
 public:
  explicit ImageView(QWidget* parent = nullptr) : QGraphicsView(parent) {
    setDragMode(QGraphicsView::ScrollHandDrag);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setResizeAnchor(QGraphicsView::AnchorViewCenter);
    setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
  }

 protected:
  void wheelEvent(QWheelEvent* event) override {
    const qreal factor = event->angleDelta().y() >= 0 ? 1.15 : (1.0 / 1.15);
    scale(factor, factor);
    event->accept();
  }
};

class ImagePreviewDialog final : public QDialog {
 public:
  explicit ImagePreviewDialog(const QPixmap& pixmap, QWidget* parent = nullptr)
      : QDialog(parent) {
    setObjectName(QStringLiteral("imagePreviewDialog"));
    setWindowTitle(QStringLiteral("图片预览"));
    resize(900, 700);

    auto* layout = new QVBoxLayout(this);
    view_ = new ImageView(this);
    view_->setObjectName(QStringLiteral("imagePreviewView"));
    scene_ = new QGraphicsScene(this);
    item_ = scene_->addPixmap(pixmap);
    view_->setScene(scene_);
    layout->addWidget(view_, 1);

    auto* buttons = new QHBoxLayout;
    auto* fit = new QPushButton(QStringLiteral("适应窗口"), this);
    fit->setObjectName(QStringLiteral("imagePreviewFit"));
    auto* actual = new QPushButton(QStringLiteral("100%"), this);
    actual->setObjectName(QStringLiteral("imagePreviewActual"));
    auto* close = new QPushButton(QStringLiteral("关闭"), this);
    close->setObjectName(QStringLiteral("imagePreviewClose"));
    buttons->addWidget(fit);
    buttons->addWidget(actual);
    buttons->addStretch(1);
    buttons->addWidget(close);
    layout->addLayout(buttons);

    connect(fit, &QPushButton::clicked, this, [this] { FitImage(); });
    connect(actual, &QPushButton::clicked, this, [this] { ActualSize(); });
    connect(close, &QPushButton::clicked, this, &QDialog::accept);
    QTimer::singleShot(0, this, [this] { FitImage(); });
  }

 private:
  void FitImage() {
    if (item_ != nullptr) {
      view_->fitInView(item_, Qt::KeepAspectRatio);
    }
  }

  void ActualSize() {
    view_->resetTransform();
    if (item_ != nullptr) {
      view_->centerOn(item_);
    }
  }

  ImageView* view_ = nullptr;
  QGraphicsScene* scene_ = nullptr;
  QGraphicsPixmapItem* item_ = nullptr;
};

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

MdViewerWindow::MdViewerWindow(MdViewerSettings* settings, QWidget* parent)
    : QMainWindow(parent), settings_(settings) {
  Q_ASSERT(settings_ != nullptr);
  setAcceptDrops(true);
  createActions();
  initUi();
  initSignals();
  if (!settings_->window_geometry.isEmpty()) {
    restoreGeometry(settings_->window_geometry);
  }
  if (!settings_->dock_state.isEmpty()) {
    restoreState(settings_->dock_state);
  }
}

// qApp->quit() 不会走 closeEvent，析构时补一次落盘
MdViewerWindow::~MdViewerWindow() { SaveSettings(); }

void MdViewerWindow::createActions() {
  open_file_action_ = new QAction(QStringLiteral("打开文件…"), this);
  open_file_action_->setShortcut(QKeySequence::Open);
  open_dir_action_ = new QAction(QStringLiteral("打开目录…"), this);
  quit_action_ = new QAction(QStringLiteral("退出"), this);
  quit_action_->setShortcut(QKeySequence::Quit);
  about_action_ = new QAction(QStringLiteral("关于"), this);
  export_html_action_ = new QAction(QStringLiteral("导出 HTML…"), this);
  export_html_action_->setObjectName(QStringLiteral("exportHtmlAction"));
  export_pdf_action_ = new QAction(QStringLiteral("导出 PDF…"), this);

  // 查找动作不建菜单：快捷键挂在窗口上，按钮内嵌进标题栏搜索框
  find_action_ = new QAction(QStringLiteral("查找…"), this);
  find_action_->setShortcut(QKeySequence::Find);
  find_next_action_ = new QAction(QStringLiteral("下一个"), this);
  find_next_action_->setShortcut(QKeySequence(Qt::Key_F3));
  find_next_action_->setToolTip(QStringLiteral("查找下一个（F3）"));
  find_prev_action_ = new QAction(QStringLiteral("上一个"), this);
  find_prev_action_->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_F3));
  find_prev_action_->setToolTip(QStringLiteral("查找上一个（Shift+F3）"));
  case_action_ = new QAction(QStringLiteral("区分大小写"), this);
  case_action_->setCheckable(true);
  case_action_->setToolTip(QStringLiteral("区分大小写"));
  word_action_ = new QAction(QStringLiteral("全词匹配"), this);
  word_action_->setCheckable(true);
  word_action_->setToolTip(QStringLiteral("全词匹配"));
}

void MdViewerWindow::initUi() {
  setWindowTitle(QStringLiteral("Markdown 查看器"));
  resize(1100, 800);

  // 渲染全部由模块负责：加载、样式、缩放、锚点、代码块复制都是它的内置能力
  view_ = new MarkdownView(this);
  // 主题与渲染开关要在面板构造前落到视图上，面板才能显示正确的初始值
  view_->SetTheme(settings_->dark_theme ? MarkdownTheme::Dark
                                        : MarkdownTheme::Light);
  view_->SetCodeLineNumbers(settings_->code_line_numbers);
  view_->SetFormulaRenderingEnabled(settings_->formula_rendering);
  view_->SetMermaidRenderingEnabled(settings_->mermaid_rendering);
  view_->SetPersistentRenderCacheEnabled(settings_->persistent_cache);
  setCentralWidget(view_);

  // 导航工具条：文件跳转和锚点跳转共用一个历史栈
  auto* toolbar = addToolBar(QStringLiteral("导航"));
  toolbar->setObjectName(QStringLiteral("viewerToolBar"));
  toolbar->setMovable(false);
  toolbar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
  toolbar->setIconSize(QSize(16, 16));

  back_action_ = toolbar->addAction(QStringLiteral("后退"));
  back_action_->setObjectName(QStringLiteral("navBack"));
  back_action_->setIcon(style()->standardIcon(QStyle::SP_ArrowBack));
  back_action_->setShortcut(QKeySequence(QStringLiteral("Ctrl+Alt+Left")));
  back_action_->setToolTip(QStringLiteral("后退（Ctrl+Alt+←）"));

  forward_action_ = toolbar->addAction(QStringLiteral("前进"));
  forward_action_->setObjectName(QStringLiteral("navForward"));
  forward_action_->setIcon(style()->standardIcon(QStyle::SP_ArrowForward));
  forward_action_->setShortcut(QKeySequence(QStringLiteral("Ctrl+Alt+Right")));
  forward_action_->setToolTip(QStringLiteral("前进（Ctrl+Alt+→）"));

  toolbar->addSeparator();

  // 缩放快捷键由 MarkdownView 内部持有，这里的按钮不再重复注册快捷键
  zoom_out_action_ = toolbar->addAction(QStringLiteral("缩小"));
  zoom_out_action_->setObjectName(QStringLiteral("zoomOut"));
  zoom_out_action_->setToolTip(QStringLiteral("缩小（Ctrl+-）"));

  zoom_reset_action_ = toolbar->addAction(QStringLiteral("100%"));
  zoom_reset_action_->setObjectName(QStringLiteral("zoomReset"));
  zoom_reset_action_->setToolTip(QStringLiteral("重置缩放（Ctrl+0）"));

  zoom_in_action_ = toolbar->addAction(QStringLiteral("放大"));
  zoom_in_action_->setObjectName(QStringLiteral("zoomIn"));
  zoom_in_action_->setToolTip(QStringLiteral("放大（Ctrl+=）"));
  UpdateHistoryActions();

  QMenu* file_menu = menuBar()->addMenu(QStringLiteral("文件(&F)"));
  file_menu->addAction(open_file_action_);
  file_menu->addAction(open_dir_action_);
  recent_menu_ = file_menu->addMenu(QStringLiteral("最近打开"));
  file_menu->addSeparator();
  file_menu->addAction(export_html_action_);
  file_menu->addAction(export_pdf_action_);
  file_menu->addSeparator();
  file_menu->addAction(quit_action_);

  QMenu* view_menu = menuBar()->addMenu(QStringLiteral("视图(&V)"));
  QMenu* help_menu = menuBar()->addMenu(QStringLiteral("帮助(&H)"));
  help_menu->addAction(about_action_);

  // 没有菜单承载也要让快捷键生效：动作挂到窗口上
  addAction(find_action_);
  addAction(find_next_action_);
  addAction(find_prev_action_);

  // 目录里扫到的 Markdown 铺在这：显示文件名，UserRole 存全路径
  auto* dock = new QDockWidget(QStringLiteral("文档"), this);
  dock->setObjectName(QStringLiteral("documentDock"));
  file_list_ = new QListWidget(dock);
  file_list_->setAlternatingRowColors(true);
  dock->setWidget(file_list_);
  addDockWidget(Qt::LeftDockWidgetArea, dock);
  view_menu->addAction(dock->toggleViewAction());

  // 大纲：标题层级 + 点击回跳；当前章节由本类按滚动位置反查
  auto* outline_dock = new QDockWidget(QStringLiteral("大纲"), this);
  outline_dock->setObjectName(QStringLiteral("outlineDock"));
  outline_ = new MarkdownOutlineWidget(outline_dock);
  outline_dock->setWidget(outline_);
  addDockWidget(Qt::LeftDockWidgetArea, outline_dock);
  view_menu->addAction(outline_dock->toggleViewAction());

  // 渲染状态：引擎加载情况与渲染/命中计数，独立成 dock 便于常开观察
  auto* status_dock = new QDockWidget(QStringLiteral("渲染状态"), this);
  status_dock->setObjectName(QStringLiteral("renderStatusDock"));
  render_status_ = new RenderStatusPanel(view_, status_dock);
  status_dock->setWidget(render_status_);
  addDockWidget(Qt::RightDockWidgetArea, status_dock);
  view_menu->addAction(status_dock->toggleViewAction());
  // 默认收起；需要时从“视图”菜单打开。已保存的 dock 布局会覆盖这个默认值
  status_dock->hide();

  // 渲染设置：主题、行号、渲染开关、持久缓存、诊断
  auto* settings_dock = new QDockWidget(QStringLiteral("渲染设置"), this);
  settings_dock->setObjectName(QStringLiteral("renderSettingsDock"));
  settings_dock->setMinimumWidth(280);
  render_settings_ = new RenderSettingsPanel(view_, settings_dock);
  render_settings_->SetWindowBlurEnabled(settings_->window_blur);
  settings_dock->setWidget(render_settings_);
  addDockWidget(Qt::RightDockWidgetArea, settings_dock);
  view_menu->addAction(settings_dock->toggleViewAction());
  // 同上：渲染设置默认收起
  settings_dock->hide();

  statusBar()->setObjectName(QStringLiteral("mainStatusBar"));
  path_label_ = new QLabel(this);
  render_progress_ = new QProgressBar(this);
  render_progress_->setObjectName(QStringLiteral("renderProgressBar"));
  render_progress_->setTextVisible(true);
  render_progress_->setFormat(QStringLiteral("渲染 %v/%m"));
  render_progress_->setFixedWidth(160);
  render_progress_->setToolTip(QStringLiteral("当前文档的渲染任务进度"));
  render_progress_->hide();
  statusBar()->addPermanentWidget(render_progress_);
  statusBar()->addPermanentWidget(path_label_);
  zoom_label_ = new QLabel(this);
  zoom_label_->setObjectName(QStringLiteral("zoomLabel"));
  zoom_label_->setToolTip(
      QStringLiteral("Ctrl+滚轮 / Ctrl+= / Ctrl+- / Ctrl+0"));
  statusBar()->addPermanentWidget(zoom_label_);
  UpdateZoomLabel(view_->zoom());

  // 无边框壳 + Fluent 标题栏；菜单栏挂进标题栏（与 FluentUI Gallery 同款）
  fluent_frame_ = new FluentWindowFrame(this, this);
  fluent_frame_->installChromeHeader(menuBar());
  // 强调色按钮只在标题栏内部改选中态并发信号，查看器没有消费方，隐藏
  for (QToolButton* button : fluent_frame_->titleBar()->accentButtons()) {
    button->setVisible(false);
  }
  // 与文档主题初始一致；标题栏据此选搜索图标等自身角色的配色
  fluent_frame_->titleBar()->setThemeDark(view_->theme() !=
                                          MarkdownTheme::Light);
#ifdef Q_OS_WIN
  // DWM 在窗口后面做模糊（Win8+ 走 SetWindowCompositionAttribute）。
  // QWK 在拿不到 HWND 时会缓存属性，show 后自动补应用
  if (settings_->window_blur) {
    fluent_frame_->setWindowBackdrop(QStringLiteral("dwm-blur"));
  }
#endif

  // 直接用标题栏自带的搜索框：模式开关与上一个/下一个内嵌成 Trailing action
  search_edit_ = fluent_frame_->titleBar()->searchLineEdit();
  search_edit_->setObjectName(QStringLiteral("titleSearchEdit"));
  search_edit_->addAction(case_action_, QLineEdit::TrailingPosition);
  search_edit_->addAction(word_action_, QLineEdit::TrailingPosition);
  // 上一个/下一个不内嵌到搜索框：它们的 QAction 仍挂在窗口上，F3/Shift+F3
  // 照常可用；输入防抖后手动跳转的需求也少了
  search_timer_ = new QTimer(this);
  search_timer_->setSingleShot(true);
  search_timer_->setInterval(250);
  UpdateSearchActionIcons();
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
  connect(render_settings_, &RenderSettingsPanel::windowBlurEnabledChanged,
          this, &MdViewerWindow::SetWindowBlur);
  connect(back_action_, &QAction::triggered, this, &MdViewerWindow::GoBack);
  connect(forward_action_, &QAction::triggered, this,
          &MdViewerWindow::GoForward);
  connect(zoom_in_action_, &QAction::triggered, view_, &MarkdownView::ZoomIn);
  connect(zoom_out_action_, &QAction::triggered, view_, &MarkdownView::ZoomOut);
  connect(zoom_reset_action_, &QAction::triggered, view_,
          &MarkdownView::ResetZoom);
  connect(open_file_action_, &QAction::triggered, this,
          &MdViewerWindow::OpenFileDialog);
  connect(open_dir_action_, &QAction::triggered, this,
          &MdViewerWindow::OpenDirectoryDialog);
  connect(quit_action_, &QAction::triggered, this, &QWidget::close);
  connect(about_action_, &QAction::triggered, this, [this] {
    QMessageBox::about(this, QStringLiteral("关于"),
                       QStringLiteral("Markdown 查看器——examples 里的宿主示例，"
                                      "渲染由 src/markdown 提供。\n\n"
                                      "作者：zhouyonghua\n"
                                      "邮箱：zhouyohu@163.com"));
  });
  // 点列表换文档。UserRole 存的是全路径
  connect(file_list_, &QListWidget::itemClicked, this,
          [this](QListWidgetItem* item) {
            OpenPath(item->data(Qt::UserRole).toString());
          });
  connect(view_, &MarkdownView::outlineChanged, this,
          [this] { outline_->SetHeadings(view_->outline()); });
  connect(view_, &MarkdownView::zoomChanged, this,
          &MdViewerWindow::UpdateZoomLabel);
  connect(view_, &MarkdownView::renderStatusChanged, this,
          &MdViewerWindow::ReportRenderStatus);
  connect(view_, &MarkdownView::documentReloaded, this, [this] {
    statusBar()->showMessage(QStringLiteral("文件已更新，已重新加载"), 3000);
  });
  connect(view_, &MarkdownView::renderProgress, this,
          [this](quint64 request_id, int completed, int total, int in_flight) {
            Q_UNUSED(in_flight);
            progress_request_ = request_id;
            if (total > 0) {
              render_progress_->setRange(0, total);
              render_progress_->setValue(completed);
              render_progress_->setFormat(QStringLiteral("渲染 %v/%m"));
              render_progress_->show();
            } else {
              render_progress_->hide();
            }
          });
  connect(view_, &MarkdownView::renderFinished, this, [this](quint64) {
    render_progress_->hide();
    render_progress_->setRange(0, 1);
    render_progress_->setValue(0);
    progress_request_ = 0;
    if (!pending_fragment_.isEmpty()) {
      const QString fragment = pending_fragment_;
      pending_fragment_.clear();
      ScrollToFragment(fragment);
    }
  });
  connect(view_, &MarkdownView::codeCopied, this, [this] {
    statusBar()->showMessage(QStringLiteral("已复制代码块"), 2000);
  });
  connect(view_, &MarkdownView::imageClicked, this, &MdViewerWindow::OpenImage);
  connect(view_, &MarkdownView::imageContextMenuRequested, this,
          &MdViewerWindow::ShowImageContextMenu);
  connect(view_, &MarkdownView::resourceLoadFailed, this,
          [this](const QUrl& url) {
            const QString label = url.isLocalFile()
                                      ? QFileInfo(url.toLocalFile()).fileName()
                                      : url.toString();
            statusBar()->showMessage(
                QStringLiteral("图片加载失败：%1").arg(label), 5000);
          });
  connect(outline_, &MarkdownOutlineWidget::headingActivated, this,
          [this](const QString& anchor) {
            NavigateTo(view_->current_file(), anchor, /*record_history=*/true);
          });
  connect(render_settings_, &RenderSettingsPanel::statusMessage, this,
          [this](const QString& message) {
            statusBar()->showMessage(message, 5000);
          });
  connect(render_status_, &RenderStatusPanel::statusMessage, this,
          [this](const QString& message) {
            statusBar()->showMessage(message, 5000);
          });
  // 当前章节由宿主反查：最后一个 AnchorY <= 滚动位置 的标题。
  // 标题自带 margin，顶部标题的 y 通常大于 0；滚动到顶时留一段容差，
  // 否则第一项永远选不中
  connect(view_, &MarkdownView::viewportScrolled, this, [this](int y) {
    constexpr int kAnchorSlackPx = 32;
    QString current;
    for (const MarkdownHeading& heading : view_->outline()) {
      const std::optional<int> anchor_y = view_->AnchorY(heading.anchor);
      if (!anchor_y.has_value()) {
        continue;
      }
      if (*anchor_y > y + kAnchorSlackPx) {
        break;
      }
      current = heading.anchor;
    }
    outline_->SetCurrentAnchor(current);
  });

  connect(view_, &MarkdownView::loadFailed, this,
          [this](const QString& path, const QString& reason) {
            statusBar()->showMessage(
                QStringLiteral("打开失败：%1（%2）")
                    .arg(QFileInfo(path).fileName(), reason),
                5000);
          });

  connect(export_html_action_, &QAction::triggered, this,
          &MdViewerWindow::ExportHtml);
  connect(export_pdf_action_, &QAction::triggered, this,
          &MdViewerWindow::ExportPdf);
  // 链接策略由宿主决定：
  // 页内锚点只滚；本地 .md 换篇（带锚点就载入后滚过去）；其余交系统。
  // 锚点判定要比对"去掉 fragment 是不是当前文档的目录"——qlitehtml 把纯
  // fragment 拼到基准 URL（目录）上，判 isLocalFile/路径空永远判不中
  connect(view_, &MarkdownView::linkClicked, this, [this](const QUrl& url) {
    const QString fragment = url.fragment();
    if (!fragment.isEmpty()) {
      const QUrl no_frag = url.adjusted(QUrl::RemoveFragment);
      const QUrl here =
          view_->has_document()
              ? QUrl::fromLocalFile(
                    QFileInfo(view_->current_file()).absolutePath() +
                    QLatin1Char('/'))
              : QUrl();
      if (no_frag.isEmpty() || no_frag == here) {
        NavigateTo(view_->current_file(), fragment, /*record_history=*/true);
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
  connect(find_action_, &QAction::triggered, this,
          &MdViewerWindow::FocusSearch);
  connect(find_next_action_, &QAction::triggered, this,
          [this] { FindNext(/*backward=*/false); });
  connect(find_prev_action_, &QAction::triggered, this,
          [this] { FindNext(/*backward=*/true); });
  connect(case_action_, &QAction::toggled, this, [this] {
    UpdateSearchActionIcons();
    statusBar()->showMessage(case_action_->isChecked()
                                 ? QStringLiteral("区分大小写：开")
                                 : QStringLiteral("区分大小写：关"),
                             2000);
    search_timer_->stop();
    if (!search_edit_->text().isEmpty()) {
      FindNext(/*backward=*/false);
    }
  });
  connect(word_action_, &QAction::toggled, this, [this] {
    UpdateSearchActionIcons();
    statusBar()->showMessage(word_action_->isChecked()
                                 ? QStringLiteral("全词匹配：开")
                                 : QStringLiteral("全词匹配：关"),
                             2000);
    search_timer_->stop();
    if (!search_edit_->text().isEmpty()) {
      FindNext(/*backward=*/false);
    }
  });
  // 边打字边找：防抖，停下来 250ms 才搜；incremental 保证换关键词时
  // 从当前命中处继续，而不是跳到下一个
  connect(search_edit_, &QLineEdit::textChanged, this,
          [this](const QString& text) {
            if (text.isEmpty()) {
              search_timer_->stop();
              view_->ClearSelection();
              return;
            }
            search_timer_->start();
          });
  connect(search_timer_, &QTimer::timeout, this,
          [this] { FindNext(/*backward=*/false, /*incremental=*/true); });
  // 回车 = 立即下一个，并取消挂起的自动搜索
  connect(search_edit_, &QLineEdit::returnPressed, this, [this] {
    search_timer_->stop();
    FindNext(/*backward=*/false);
  });
  // Esc 把焦点还给正文；搜索框与它的 action 保持常驻，不再有"关闭查找栏"
  auto* escape = new QShortcut(QKeySequence(Qt::Key_Escape), this);
  connect(escape, &QShortcut::activated, this, [this] {
    if (search_edit_->hasFocus()) {
      view_->setFocus();
    }
  });
}

void MdViewerWindow::OpenPath(const QString& path) {
  NavigateTo(path, QString(), /*record_history=*/true);
}

void MdViewerWindow::NavigateTo(const QString& path, const QString& fragment,
                                bool record_history) {
  // 正在渲染时旧 HTML 还在屏幕上：同目标只更新待滚锚点，别再滚旧文档
  if (view_->is_rendering() && path == view_->pending_file()) {
    pending_fragment_ = fragment;
    if (record_history) {
      PushHistory(path, fragment);
    }
    return;
  }
  // 同文件只滚锚点，不重加载；否则后退到同文档的另一个锚点会闪一下
  if (!view_->current_file().isEmpty() && path == view_->current_file()) {
    if (view_->is_rendering()) {
      // 用户回到了屏幕上已提交的文档：放弃还没提交的换篇
      view_->CancelPendingRender();
      pending_fragment_.clear();
      render_progress_->hide();
      progress_request_ = 0;
    }
    if (record_history) {
      PushHistory(path, fragment);
    }
    ScrollToFragment(fragment);
    return;
  }
  // 新文档的锚点要等提交后再滚；旧文档的 heading_ids_ 里没有它
  pending_fragment_ = fragment;
  const QString opened = OpenPathCore(path);
  if (opened.isEmpty()) {
    pending_fragment_.clear();
    return;
  }
  if (record_history) {
    PushHistory(opened, fragment);
  }
  if (fragment.isEmpty()) {
    pending_fragment_.clear();
    ScrollToFragment(QString());
  }
}

QString MdViewerWindow::OpenPathCore(const QString& path) {
  const QFileInfo info(path);
  if (info.isDir()) {
    if (!RefreshDirectoryListing(info.absoluteFilePath())) {
      return {};
    }
    const QString first = file_list_->item(0)->data(Qt::UserRole).toString();
    return ShowFile(first) ? first : QString();
  }
  const QString absolute = info.absoluteFilePath();
  return ShowFile(absolute) ? absolute : QString();
}

void MdViewerWindow::PushHistory(const QString& path, const QString& fragment) {
  while (history_.size() > history_index_ + 1) {
    history_.removeLast();
  }
  const HistoryEntry entry{path, fragment};
  if (history_.isEmpty() || !(history_.last() == entry)) {
    history_.append(entry);
  }
  history_index_ = history_.size() - 1;
  UpdateHistoryActions();
}

void MdViewerWindow::ScrollToFragment(const QString& fragment) {
  if (fragment.isEmpty()) {
    view_->SetScrollPosition(0);
    return;
  }
  view_->ScrollToAnchor(fragment);
}

void MdViewerWindow::GoBack() {
  if (history_index_ <= 0) {
    return;
  }
  --history_index_;
  const HistoryEntry entry = history_.at(history_index_);
  NavigateTo(entry.path, entry.fragment, /*record_history=*/false);
  UpdateHistoryActions();
}

void MdViewerWindow::GoForward() {
  if (history_index_ + 1 >= history_.size()) {
    return;
  }
  ++history_index_;
  const HistoryEntry entry = history_.at(history_index_);
  NavigateTo(entry.path, entry.fragment, /*record_history=*/false);
  UpdateHistoryActions();
}

void MdViewerWindow::UpdateHistoryActions() {
  if (back_action_ != nullptr) {
    back_action_->setEnabled(history_index_ > 0);
  }
  if (forward_action_ != nullptr) {
    forward_action_->setEnabled(history_index_ + 1 < history_.size());
  }
}

void MdViewerWindow::OpenLastFile() {
  for (const QString& path : settings_->recent_files) {
    if (QFileInfo(path).isFile()) {
      const QString opened = OpenPathCore(path);
      if (!opened.isEmpty()) {
        PushHistory(opened, QString());
        statusBar()->showMessage(QStringLiteral("已恢复上次打开：%1")
                                     .arg(QFileInfo(opened).fileName()),
                                 3000);
      }
      return;
    }
  }
}

void MdViewerWindow::OpenFileDialog() {
  const QString start_dir =
      listed_dir_.isEmpty() ? QDir::homePath() : listed_dir_;
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
      listed_dir_.isEmpty() ? QDir::homePath() : listed_dir_);
  if (!dir.isEmpty()) {
    OpenPath(dir);
  }
}

void MdViewerWindow::ApplyTheme(MarkdownTheme theme) {
  const bool dark = theme == MarkdownTheme::Dark;
  qApp->setProperty("_q_colorscheme", dark ? 1 : 0);
  // 重建样式，让 FluentUI3Style 按新的配色方案重算调色板（Gallery 同款）
  qApp->setStyle(new FluentUI3Style);
  fluent_frame_->titleBar()->setThemeDark(dark);
  // 搜索框里的 A/T/箭头是运行时按调色板画的，换肤后要重画
  UpdateSearchActionIcons();
  const bool theme_changed = view_->theme() != theme;
  view_->SetTheme(theme);
  if (theme_changed && (view_->has_document() || view_->is_rendering())) {
    // 换肤同样要等整篇重渲染；否则旧配色会一直留在屏幕上
    view_->ShowPlaceholder(QStringLiteral("正在渲染…"));
  }
  // 面板是被动同步方，用 QSignalBlocker 防止再发一次 themeChanged
  render_settings_->SetTheme(theme);
  statusBar()->showMessage(theme == MarkdownTheme::Dark
                               ? QStringLiteral("已切换主题：暗色")
                               : QStringLiteral("已切换主题：浅色"),
                           3000);
}

void MdViewerWindow::ToggleTheme() {
  const bool dark = qApp->property("_q_colorscheme").toInt() == 1;
  ApplyTheme(dark ? MarkdownTheme::Light : MarkdownTheme::Dark);
}

void MdViewerWindow::SetWindowBlur(bool enabled) {
#ifdef Q_OS_WIN
  // 只切 backdrop 不够：关掉 blur 后调色板若仍透明，窗口会露底。
  // _q_widget_mode 与样式要一起切
  qApp->setProperty("_q_widget_mode", enabled ? 2 : 0);
  qApp->setStyle(new FluentUI3Style);
  if (enabled) {
    fluent_frame_->setWindowBackdrop(QStringLiteral("dwm-blur"));
  } else {
    fluent_frame_->clearWindowBackdrop();
  }
  statusBar()->showMessage(enabled ? QStringLiteral("窗口模糊已开启")
                                   : QStringLiteral("窗口模糊已关闭"),
                           3000);
#else
  Q_UNUSED(enabled);
#endif
}

bool MdViewerWindow::RefreshDirectoryListing(const QString& dir) {
  const QFileInfoList files = QDir(dir).entryInfoList(
      MarkdownFilters(), QDir::Files, QDir::Name | QDir::IgnoreCase);
  if (files.isEmpty()) {
    statusBar()->showMessage(
        QStringLiteral("目录中没有 Markdown 文件：%1").arg(dir), 5000);
    return false;
  }
  file_list_->clear();
  for (const QFileInfo& info : files) {
    auto* item = new QListWidgetItem(info.fileName(), file_list_);
    item->setData(Qt::UserRole, info.absoluteFilePath());
    item->setToolTip(info.absoluteFilePath());
  }
  listed_dir_ = dir;
  statusBar()->showMessage(
      QStringLiteral("%1：%2 篇 Markdown").arg(dir).arg(files.size()), 5000);
  return true;
}

bool MdViewerWindow::ShowFile(const QString& path) {
  const QFileInfo info(path);
  // 单篇打开时顺手把同目录的兄弟列出来——侧栏不该只对"打开目录"生效
  if (info.absolutePath() != listed_dir_) {
    RefreshDirectoryListing(info.absolutePath());
  }
  if (!view_->LoadFile(info.absoluteFilePath())) {
    return false;  // 视图里已经是错误页，状态栏的消息由 loadFailed 负责
  }
  // 旧文档还在屏幕上，而新文档已在后台渲染；先给一个明确的等待页
  view_->ShowPlaceholder(QStringLiteral("正在渲染…"));
  setWindowTitle(QStringLiteral("Markdown 查看器 - %1").arg(info.fileName()));
  path_label_->setText(info.fileName());
  path_label_->setToolTip(info.absoluteFilePath());
  SelectInList(info.absoluteFilePath());
  // 最近记录立即落盘：即使随后异常退出，也不会丢这次打开
  settings_->AddRecentFile(info.absoluteFilePath());
  RefreshRecentMenu();
  return true;
}

void MdViewerWindow::SelectInList(const QString& path) {
  for (int row = 0; row < file_list_->count(); ++row) {
    QListWidgetItem* item = file_list_->item(row);
    if (item->data(Qt::UserRole).toString() == path) {
      file_list_->setCurrentItem(item);
      file_list_->scrollToItem(item);
      return;
    }
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
    connect(action, &QAction::triggered, this,
            [this, path] { OpenPath(path); });
  }
  recent_menu_->addSeparator();
  QAction* clear = recent_menu_->addAction(QStringLiteral("清除最近记录"));
  connect(clear, &QAction::triggered, this, [this] {
    settings_->recent_files.clear();
    settings_->SaveRecent();
    RefreshRecentMenu();
  });
}

void MdViewerWindow::SaveSettings() {
  if (settings_ == nullptr || view_ == nullptr) {
    return;
  }
  settings_->window_geometry = saveGeometry();
  settings_->dock_state = saveState();
  settings_->dark_theme = view_->theme() == MarkdownTheme::Dark;
  settings_->code_line_numbers = view_->code_line_numbers();
  settings_->formula_rendering = view_->formula_rendering_enabled();
  settings_->mermaid_rendering = view_->mermaid_rendering_enabled();
  settings_->persistent_cache = view_->persistent_render_cache_enabled();
#ifdef Q_OS_WIN
  settings_->window_blur = qApp->property("_q_widget_mode").toInt() >= 1;
#endif
  settings_->Save();
}

void MdViewerWindow::UpdateZoomLabel(qreal factor) {
  zoom_label_->setText(QStringLiteral("缩放 %1%").arg(qRound(factor * 100.0)));
}

void MdViewerWindow::ReportRenderStatus() {
  const MarkdownRendererStatus now = view_->renderer_status();
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

void MdViewerWindow::FocusSearch() {
  search_edit_->setFocus();
  search_edit_->selectAll();  // 再按 Ctrl+F 是"改查找词"，不是接着找
}

void MdViewerWindow::UpdateSearchActionIcons() {
  case_action_->setIcon(
      TextActionIcon(QStringLiteral("A"), case_action_->isChecked()));
  word_action_->setIcon(
      TextActionIcon(QStringLiteral("T"), word_action_->isChecked()));
  find_prev_action_->setIcon(TextActionIcon(QStringLiteral("↑"), false));
  find_next_action_->setIcon(TextActionIcon(QStringLiteral("↓"), false));
}

void MdViewerWindow::FindNext(bool backward, bool incremental) {
  const QString needle = search_edit_->text();
  if (needle.isEmpty()) {
    return;
  }
  bool wrapped = false;
  MarkdownView::FindFlags flags =
      backward ? MarkdownView::FindBackward : MarkdownView::FindNoFlag;
  if (case_action_->isChecked()) {
    flags |= MarkdownView::FindCaseSensitively;
  }
  if (word_action_->isChecked()) {
    flags |= MarkdownView::FindWholeWords;
  }
  if (!view_->FindText(needle, flags, &wrapped, incremental)) {
    statusBar()->showMessage(QStringLiteral("未找到：%1").arg(needle), 3000);
    return;
  }
  // 绕回说明找了一圈——提示一下，否则用户以为"没动"
  if (wrapped) {
    statusBar()->showMessage(
        backward ? QStringLiteral("已绕回末尾") : QStringLiteral("已绕回开头"),
        3000);
  }
}

void MdViewerWindow::ExportHtml() {
  if (view_->is_rendering()) {
    statusBar()->showMessage(QStringLiteral("文档还在渲染，稍后再导出"), 3000);
    return;
  }
  if (!view_->has_document()) {
    statusBar()->showMessage(QStringLiteral("先打开一篇 Markdown 再导出"),
                             3000);
    return;
  }
  QString default_path;
  if (!view_->current_file().isEmpty()) {
    const QFileInfo info(view_->current_file());
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
  if (!view_->ExportHtml(path, &error)) {
    statusBar()->showMessage(
        error.isEmpty() ? QStringLiteral("导出 HTML 失败") : error, 5000);
    return;
  }
  statusBar()->showMessage(
      QStringLiteral("已导出：%1").arg(QFileInfo(path).fileName()), 5000);
}

void MdViewerWindow::OpenImage(const QUrl& url) {
  if (view_->ImageInfo(url).has_value()) {
    ShowImageSource(url);
    return;
  }
  ShowImagePreview(url);
}

void MdViewerWindow::ShowImageContextMenu(const QPoint& global_pos,
                                          const QUrl& url) {
  const QPixmap pixmap = view_->ImagePixmap(url);
  const std::optional<MarkdownImageInfo> info = view_->ImageInfo(url);

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
  const QPixmap pixmap = view_->ImagePixmap(url);
  if (pixmap.isNull()) {
    statusBar()->showMessage(QStringLiteral("这张图片无法保存"), 3000);
    return;
  }
  QString start_dir = QDir::homePath();
  if (!view_->current_file().isEmpty()) {
    start_dir = QFileInfo(view_->current_file()).absolutePath();
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
  const QPixmap pixmap = view_->ImagePixmap(url);
  if (pixmap.isNull()) {
    statusBar()->showMessage(QStringLiteral("这张图片无法预览"), 3000);
    return;
  }
  ImagePreviewDialog dialog(pixmap, this);
  dialog.exec();
}

void MdViewerWindow::ShowImageSource(const QUrl& url) {
  const std::optional<MarkdownImageInfo> info = view_->ImageInfo(url);
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
  if (view_->is_rendering()) {
    statusBar()->showMessage(QStringLiteral("文档还在渲染，稍后再导出"), 3000);
    return;
  }
  if (!view_->has_document()) {
    statusBar()->showMessage(QStringLiteral("先打开一篇 Markdown 再导出"),
                             3000);
    return;
  }
  QString default_path;
  if (!view_->current_file().isEmpty()) {
    const QFileInfo info(view_->current_file());
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
  if (view_->is_rendering()) {
    statusBar()->showMessage(QStringLiteral("文档还在渲染，稍后再导出"), 3000);
    return;
  }
  QPrinter printer(QPrinter::HighResolution);
  printer.setOutputFormat(QPrinter::PdfFormat);
  printer.setOutputFileName(path);
  view_->Print(&printer);

  statusBar()->showMessage(
      QStringLiteral("已导出：%1").arg(QFileInfo(path).fileName()), 5000);
}
