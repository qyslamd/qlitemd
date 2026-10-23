// clang-format off
#include "md_viewer_window.h"

// Must precede windows.h: winuser.h defines FindText as FindTextA/W.
#include "markdown_view.h"

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
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QFont>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QGraphicsView>
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
#include <QPainter>
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
#include <QStatusBar>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWheelEvent>

#ifdef Q_OS_WIN
#include <windows.h>
// winuser.h maps FindText to FindTextA/W; MarkdownView's method is not that.
#undef FindText
#endif
// clang-format on

#include "document_index.h"
#include "documents_panel.h"
#include "markdown_outline_widget.h"
#include "md_viewer_settings.h"
#include "quick_open.h"
#include "reader_appearance.h"
#include "render_settings_panel.h"
#include "render_status_panel.h"
#include "text_search.h"

namespace {

// 超过这个大小才提示；严格大于
constexpr qint64 kLargeDocumentBytes = 20 * 1024 * 1024;

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
  bookmarks_ = BookmarkStore::FromJson(settings_->bookmarks_json);
  positions_ = ReadingPositionStore::FromJson(settings_->positions_json);
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
  // 旧布局把大纲放在左侧；新默认是正文右侧整列，恢复后迁移一次
  if (outline_dock_ != nullptr &&
      dockWidgetArea(outline_dock_) != Qt::RightDockWidgetArea) {
    addDockWidget(Qt::RightDockWidgetArea, outline_dock_);
    outline_dock_->show();
  }
  resizeDocks({document_dock_, outline_dock_}, {300, 212}, Qt::Horizontal);
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

  // 渲染全部由模块负责：加载、样式、缩放、锚点、代码块复制都是它的内置能力
  view_ = new MarkdownView(this);
  // 主题与渲染开关要在面板构造前落到视图上，面板才能显示正确的初始值
  view_->SetTheme(settings_->dark_theme ? MarkdownTheme::Dark
                                        : MarkdownTheme::Light);
  view_->SetCodeLineNumbers(settings_->code_line_numbers);
  view_->SetFormulaRenderingEnabled(settings_->formula_rendering);
  view_->SetMermaidRenderingEnabled(settings_->mermaid_rendering);
  view_->SetPersistentRenderCacheEnabled(settings_->persistent_cache);

  // 阅读区顶部面包屑；正文仍由 MarkdownView 独占剩余空间
  auto* reader = new QWidget(this);
  reader->setObjectName(QStringLiteral("readerContainer"));
  auto* reader_layout = new QVBoxLayout(reader);
  reader_layout->setContentsMargins(0, 0, 0, 0);
  reader_layout->setSpacing(0);
  breadcrumb_ = new QLabel(reader);
  breadcrumb_->setObjectName(QStringLiteral("readingBreadcrumb"));
  breadcrumb_->setContentsMargins(16, 6, 16, 6);
  breadcrumb_->setForegroundRole(QPalette::PlaceholderText);
  breadcrumb_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
  breadcrumb_->setTextInteractionFlags(Qt::NoTextInteraction);
  QFont crumb_font = font();
  if (crumb_font.pointSizeF() > 0.0) {
    crumb_font.setPointSizeF(qMax(8.0, crumb_font.pointSizeF() - 1.0));
  }
  breadcrumb_->setFont(crumb_font);
  breadcrumb_->hide();
  reader_layout->addWidget(breadcrumb_);
  reader_layout->addWidget(view_, 1);
  setCentralWidget(reader);

  // 递归文档树 / 书签 / 搜索结果的侧栏
  document_dock_ = new QDockWidget(QStringLiteral("文档"), this);
  document_dock_->setObjectName(QStringLiteral("documentDock"));
  document_dock_->setFeatures(QDockWidget::NoDockWidgetFeatures);
  // 原型侧栏没有 dock 标题栏；显示/关闭走“视图”菜单
  auto* sidebar_header = new QWidget(document_dock_);
  sidebar_header->setFixedHeight(0);
  document_dock_->setTitleBarWidget(sidebar_header);
  document_dock_->setMinimumWidth(278);
  // 原型侧栏是固定窄栏；不设上限时搜索结果会把左栏撑到 400 以上
  document_dock_->setMaximumWidth(320);
  documents_panel_ = new DocumentsPanel(document_dock_);
  document_dock_->setWidget(documents_panel_);
  addDockWidget(Qt::LeftDockWidgetArea, document_dock_);

  // 大纲：标题层级 + 点击回跳；原型把它放在正文右侧整列
  outline_dock_ = new QDockWidget(QStringLiteral("大纲"), this);
  outline_dock_->setObjectName(QStringLiteral("outlineDock"));
  outline_dock_->setFeatures(QDockWidget::NoDockWidgetFeatures);
  outline_ = new MarkdownOutlineWidget(outline_dock_);
  outline_dock_->setWidget(outline_);
  addDockWidget(Qt::RightDockWidgetArea, outline_dock_);

  // 渲染状态：引擎加载情况与渲染/命中计数，独立成 dock 便于常开观察
  auto* status_dock = new QDockWidget(QStringLiteral("渲染状态"), this);
  status_dock->setObjectName(QStringLiteral("renderStatusDock"));
  render_status_ = new RenderStatusPanel(view_, status_dock);
  status_dock->setWidget(render_status_);
  addDockWidget(Qt::RightDockWidgetArea, status_dock);
  // 默认收起；需要时从“视图”菜单打开。已保存的 dock 布局会覆盖这个默认值
  status_dock->hide();

  // 渲染设置：主题、行号、渲染开关、持久缓存、诊断
  settings_dock_ = new QDockWidget(QStringLiteral("渲染设置"), this);
  settings_dock_->setObjectName(QStringLiteral("renderSettingsDock"));
  settings_dock_->setMinimumWidth(280);
  render_settings_ = new RenderSettingsPanel(view_, settings_dock_);
  render_settings_->SetWindowMaterial(settings_->window_material);
  render_settings_->SetReadingStyle(settings_->reading_style);
  render_settings_->SetTransparentReading(settings_->transparent_reading);
  settings_dock_->setWidget(render_settings_);
  addDockWidget(Qt::RightDockWidgetArea, settings_dock_);
  // 同上：渲染设置默认收起
  settings_dock_->hide();

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
  stats_layout->addWidget(backend_label_);
  stats_layout->addWidget(zoom_label_);
  statusBar()->addPermanentWidget(render_progress_);
  statusBar()->addPermanentWidget(stats_widget);
  UpdateZoomLabel(view_->zoom());

  // 无边框壳 + Fluent 标题栏；菜单栏不再单独占一行
  fluent_frame_ = new FluentWindowFrame(this, this);
  fluent_frame_->installChromeHeader(nullptr);
  // 强调色按钮只在标题栏内部改选中态并发信号，查看器没有消费方，隐藏
  for (QToolButton* button : fluent_frame_->titleBar()->accentButtons()) {
    button->setVisible(false);
  }
  // 与文档主题初始一致；标题栏据此选搜索图标等自身角色的配色
  fluent_frame_->titleBar()->setThemeDark(view_->theme() !=
                                          MarkdownTheme::Light);

  // 快速跳转：统一入口，Ctrl+P 与 VS Code 的快速打开同键
  quick_open_action_ = new QAction(QStringLiteral("快速跳转…"), this);
  quick_open_action_->setObjectName(QStringLiteral("quickOpenAction"));
  quick_open_action_->setIcon(
      QIcon(QStringLiteral(":/resources/svg/search.svg")));
  quick_open_action_->setShortcut(QKeySequence(QStringLiteral("Ctrl+P")));
  quick_open_action_->setToolTip(
      QStringLiteral("跳转到文件、标题或命令（Ctrl+P）"));

  // 导航与缩放从工具条搬进标题栏；文件跳转和锚点跳转共用一个历史栈
  back_action_ = new QAction(QStringLiteral("后退"), this);
  back_action_->setObjectName(QStringLiteral("navBackAction"));
  back_action_->setIcon(style()->standardIcon(QStyle::SP_ArrowBack));
  back_action_->setShortcut(QKeySequence(QStringLiteral("Ctrl+Alt+Left")));
  back_action_->setToolTip(QStringLiteral("后退（Ctrl+Alt+←）"));

  forward_action_ = new QAction(QStringLiteral("前进"), this);
  forward_action_->setObjectName(QStringLiteral("navForwardAction"));
  forward_action_->setIcon(style()->standardIcon(QStyle::SP_ArrowForward));
  forward_action_->setShortcut(QKeySequence(QStringLiteral("Ctrl+Alt+Right")));
  forward_action_->setToolTip(QStringLiteral("前进（Ctrl+Alt+→）"));

  // 缩放快捷键由 MarkdownView 内部持有，这里的按钮不再重复注册快捷键
  zoom_out_action_ = new QAction(QStringLiteral("−"), this);
  zoom_out_action_->setObjectName(QStringLiteral("zoomOutAction"));
  zoom_out_action_->setToolTip(QStringLiteral("缩小（Ctrl+-）"));

  zoom_reset_action_ = new QAction(QStringLiteral("100%"), this);
  zoom_reset_action_->setObjectName(QStringLiteral("zoomResetAction"));
  zoom_reset_action_->setToolTip(QStringLiteral("重置缩放（Ctrl+0）"));

  zoom_in_action_ = new QAction(QStringLiteral("+"), this);
  zoom_in_action_->setObjectName(QStringLiteral("zoomInAction"));
  zoom_in_action_->setToolTip(QStringLiteral("放大（Ctrl+=）"));

  bookmark_action_ = new QAction(QStringLiteral("收藏当前文档"), this);
  bookmark_action_->setObjectName(QStringLiteral("bookmarkAction"));
  bookmark_action_->setShortcut(QKeySequence(QStringLiteral("Ctrl+D")));
  bookmark_action_->setToolTip(QStringLiteral("收藏当前文档（Ctrl+D）"));
  UpdateHistoryActions();

  // 汉堡菜单在 dock 建好后创建，视图菜单才能挂上 toggleViewAction
  app_menu_ = new QMenu(this);
  QMenu* file_menu = app_menu_->addMenu(QStringLiteral("文件(&F)"));
  file_menu->addAction(open_file_action_);
  file_menu->addAction(open_dir_action_);
  recent_menu_ = file_menu->addMenu(QStringLiteral("最近打开"));
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
  view_menu_->addAction(settings_dock_->toggleViewAction());
  focus_mode_action_ = new QAction(QStringLiteral("专注模式"), this);
  focus_mode_action_->setObjectName(QStringLiteral("focusModeAction"));
  focus_mode_action_->setCheckable(true);
  focus_mode_action_->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+F")));
  connect(focus_mode_action_, &QAction::toggled, this,
          &MdViewerWindow::SetFocusMode);
  view_menu_->addAction(focus_mode_action_);
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
  addAction(quit_action_);
  addAction(find_action_);
  addAction(quick_open_action_);
  addAction(back_action_);
  addAction(forward_action_);
  addAction(bookmark_action_);
  addAction(refresh_action_);
  addAction(focus_mode_action_);

  menu_button_ = new QToolButton(fluent_frame_->titleBar());
  menu_button_->setObjectName(QStringLiteral("mainMenuButton"));
  menu_button_->setAutoRaise(true);
  const QIcon menu_icon(QStringLiteral(":/resources/svg/menu.svg"));
  if (menu_icon.isNull()) {
    menu_button_->setText(QStringLiteral("≡"));
  } else {
    menu_button_->setIcon(menu_icon);
  }
  menu_button_->setPopupMode(QToolButton::InstantPopup);
  menu_button_->setMenu(app_menu_);
  menu_button_->setToolTip(QStringLiteral("菜单"));
  fluent_frame_->titleBar()->setLeadingWidget(menu_button_);

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
  star_button_->setIcon(TextActionIcon(QStringLiteral("☆"), false));
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
  appearance_button_->setText(QStringLiteral("Aa"));
  appearance_button_->setToolTip(
      QStringLiteral("阅读外观（字号、行距、栏宽）"));
  nav_layout->addWidget(appearance_button_);
  connect(appearance_button_, &QToolButton::clicked, this,
          &MdViewerWindow::ShowReaderAppearance);

  zoom_out_button_ = add_nav_button(zoom_out_action_, QStringLiteral("zoomOut"),
                                    Qt::ToolButtonTextOnly);
  zoom_reset_button_ = add_nav_button(
      zoom_reset_action_, QStringLiteral("zoomReset"), Qt::ToolButtonTextOnly);
  zoom_in_button_ = add_nav_button(zoom_in_action_, QStringLiteral("zoomIn"),
                                   Qt::ToolButtonTextOnly);
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
  // 点树里的文件换文档；树在根外打开书签时不换根
  connect(documents_panel_, &DocumentsPanel::fileActivated, this,
          [this](const QString& path) {
            NavigateTo(path, QString(), /*record_history=*/true);
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
    const QString path = displayed_file_;
    SetBookmarkChecked(!path.isEmpty() && !bookmarks_.Contains(path));
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
      pending_restore_scroll_ = -1;
      pending_restore_path_.clear();
      ScrollToFragment(fragment);
    } else if (pending_search_hit_.has_value()) {
      const PendingJump jump = *pending_search_hit_;
      pending_search_hit_.reset();
      pending_restore_scroll_ = -1;
      pending_restore_path_.clear();
      if (jump.path == view_->current_file()) {
        JumpToSearchHit(jump.ordinal, jump.query);
      }
    } else if (pending_restore_scroll_ >= 0 &&
               pending_restore_path_ == view_->current_file()) {
      const int scroll = pending_restore_scroll_;
      pending_restore_scroll_ = -1;
      pending_restore_path_.clear();
      view_->SetScrollPosition(scroll);
    } else {
      pending_restore_scroll_ = -1;
      pending_restore_path_.clear();
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
      displayed_file_ = view_->current_file();
      UpdateBookmarkUi();
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
    settings_->SaveSession();
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

void MdViewerWindow::EnsureRootForFile(const QString& path) {
  if (!root_dir_.isEmpty() && IsPathUnderRoot(path, root_dir_)) {
    return;
  }
  SetRootDir(QFileInfo(path).absolutePath());
}

QString MdViewerWindow::OpenPathCore(const QString& path) {
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
        return ShowFile(entry.absolute_path, OpenReason::kUser)
                   ? entry.absolute_path
                   : QString();
      }
    }
    return {};
  }
  const QString absolute = info.absoluteFilePath();
  EnsureRootForFile(absolute);
  return ShowFile(absolute, OpenReason::kUser) ? absolute : QString();
}

void MdViewerWindow::PushHistory(const QString& path, const QString& fragment) {
  history_.Push({path, fragment});
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
  const std::optional<NavigationEntry> entry = history_.Back();
  if (!entry.has_value()) {
    return;
  }
  NavigateTo(entry->path, entry->fragment, /*record_history=*/false);
  UpdateHistoryActions();
}

void MdViewerWindow::GoForward() {
  const std::optional<NavigationEntry> entry = history_.Forward();
  if (!entry.has_value()) {
    return;
  }
  NavigateTo(entry->path, entry->fragment, /*record_history=*/false);
  UpdateHistoryActions();
}

void MdViewerWindow::UpdateHistoryActions() {
  if (back_action_ != nullptr) {
    back_action_->setEnabled(history_.CanGoBack());
  }
  if (forward_action_ != nullptr) {
    forward_action_->setEnabled(history_.CanGoForward());
  }
}

void MdViewerWindow::RememberCurrentPosition() {
  // 占位页的 ScrollPosition 恒为 0；渲染中记录会把上一篇的位置冲掉
  if (view_ == nullptr || !view_->has_document() || view_->is_rendering()) {
    return;
  }
  const QString path = view_->current_file();
  if (path.isEmpty()) {
    return;
  }
  positions_.Update(path, view_->ScrollPosition(),
                    QDateTime::currentSecsSinceEpoch());
  settings_->positions_json = positions_.ToJson();
  settings_->SavePositions();
}

void MdViewerWindow::RestoreSession() {
  if (!settings_->root_dir.isEmpty()) {
    const DocumentScanResult result = SetRootDir(settings_->root_dir);
    if (!result.root_exists) {
      return;  // SetRootDir 已显示空态
    }
    if (QFileInfo(settings_->last_file).isFile()) {
      const QString absolute =
          QFileInfo(settings_->last_file).absoluteFilePath();
      if (ShowFile(absolute, OpenReason::kRestore)) {
        PushHistory(absolute, QString());
      }
    }
    return;
  }
  OpenLastFile();
}

void MdViewerWindow::OpenLastFile() {
  for (const QString& path : settings_->recent_files) {
    if (!QFileInfo(path).isFile()) {
      continue;
    }
    const QString absolute = QFileInfo(path).absoluteFilePath();
    EnsureRootForFile(absolute);
    if (ShowFile(absolute, OpenReason::kRestore)) {
      PushHistory(absolute, QString());
      statusBar()->showMessage(
          QStringLiteral("已恢复上次打开：%1").arg(QFileInfo(path).fileName()),
          3000);
    }
    return;
  }
  if (breadcrumb_ != nullptr) {
    breadcrumb_->clear();
    breadcrumb_->hide();
  }
  documents_panel_->SetRootMissing(
      QString(), QStringLiteral("从“文件 > 打开目录”开始阅读"));
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

void MdViewerWindow::ShowQuickOpen() {
  if (quick_open_ == nullptr) {
    quick_open_ = new QuickOpenPopup(this);
    connect(quick_open_, &QuickOpenPopup::itemActivated, this,
            &MdViewerWindow::ActivateQuickOpenItem);
  }

  QVector<QuickOpenItem> items;
  const QString current_file = view_->current_file();
  if (view_->has_document() && !view_->is_rendering() &&
      !current_file.isEmpty()) {
    for (const MarkdownHeading& heading : view_->outline()) {
      QuickOpenItem item;
      item.kind = QuickOpenKind::kHeading;
      item.title = heading.text;
      item.subtitle = QStringLiteral("%1 · H%2")
                          .arg(QFileInfo(current_file).fileName())
                          .arg(heading.level);
      item.path = current_file;
      item.anchor = heading.anchor;
      items.append(item);
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
  add_command(view_->theme() == MarkdownTheme::Dark
                  ? QStringLiteral("切换到浅色主题")
                  : QStringLiteral("切换到深色主题"),
              QStringLiteral("外观"), QStringLiteral("toggle-theme"));
  add_command(QStringLiteral("阅读外观…"), QStringLiteral("外观"),
              QStringLiteral("appearance"));
  add_command(focus_mode_ ? QStringLiteral("退出专注模式")
                          : QStringLiteral("进入专注模式"),
              QStringLiteral("视图"), QStringLiteral("focus-mode"));
  const bool starred =
      !displayed_file_.isEmpty() && bookmarks_.Contains(displayed_file_);
  add_command(starred ? QStringLiteral("取消收藏当前文档")
                      : QStringLiteral("收藏当前文档"),
              QStringLiteral("阅读"), QStringLiteral("toggle-bookmark"));
  add_command(QStringLiteral("导出 HTML…"), QStringLiteral("导出"),
              QStringLiteral("export-html"));
  add_command(QStringLiteral("导出 PDF…"), QStringLiteral("导出"),
              QStringLiteral("export-pdf"));
  add_command(QStringLiteral("刷新文档树"), QStringLiteral("视图"),
              QStringLiteral("refresh-root"));
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
  } else if (id == QLatin1String("toggle-theme")) {
    ToggleTheme();
  } else if (id == QLatin1String("appearance")) {
    ShowReaderAppearance();
  } else if (id == QLatin1String("focus-mode")) {
    focus_mode_action_->setChecked(!focus_mode_action_->isChecked());
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
  } else if (id == QLatin1String("show-settings") &&
             settings_dock_ != nullptr) {
    settings_dock_->show();
    settings_dock_->raise();
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
  const bool theme_changed = view_->theme() != theme;
  view_->SetTheme(theme);
  if (theme_changed && (view_->has_document() || view_->is_rendering())) {
    // 换肤同样要等整篇重渲染；否则旧配色会一直留在屏幕上
    view_->ShowPlaceholder(QStringLiteral("正在渲染…"));
  }
  // 面板是被动同步方，用 QSignalBlocker 防止再发一次 themeChanged
  render_settings_->SetTheme(theme);
  RefreshChromePalette();
  UpdateReadingSurface();
  // 星标图标按调色板绘制，换肤后要重画
  UpdateBookmarkUi();
  statusBar()->showMessage(theme == MarkdownTheme::Dark
                               ? QStringLiteral("已切换主题：暗色")
                               : QStringLiteral("已切换主题：浅色"),
                           3000);
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
  if (breadcrumb_ != nullptr) {
    breadcrumb_->setPalette(app_palette);
    breadcrumb_->setForegroundRole(QPalette::PlaceholderText);
  }
}

void MdViewerWindow::ApplyReadingStyle() {
  if (view_ == nullptr || settings_ == nullptr) {
    return;
  }
  if (settings_->reading_style == ReadingStyle::kPrototype) {
    view_->SetThemeStyleSheet(MarkdownTheme::Light,
                              LoadTextResource(QStringLiteral(
                                  ":/resources/themes/viewer-light.css")));
    view_->SetThemeStyleSheet(
        MarkdownTheme::Dark,
        LoadTextResource(QStringLiteral(":/resources/themes/viewer-dark.css")));
  } else {
    view_->SetThemeStyleSheet(MarkdownTheme::Light, QString());
    view_->SetThemeStyleSheet(MarkdownTheme::Dark, QString());
  }
  ApplyReaderAppearance();
  UpdateReadingSurface();
}

QColor MdViewerWindow::ReadingPageColor() const {
  if (view_ != nullptr && view_->theme() == MarkdownTheme::Dark) {
    return QColor(0x0d, 0x11, 0x17);
  }
  return QColor(0xff, 0xff, 0xff);
}

void MdViewerWindow::UpdateReadingSurface() {
  if (view_ == nullptr || settings_ == nullptr) {
    return;
  }
  const bool transparent =
      settings_->reading_style == ReadingStyle::kPrototype &&
      settings_->transparent_reading;
  view_->SetPageBackground(transparent ? QColor(Qt::transparent)
                                       : ReadingPageColor());
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
  if (view_ == nullptr) {
    return;
  }
  const bool prototype = settings_ != nullptr &&
                         settings_->reading_style == ReadingStyle::kPrototype;
  const ReaderPreferences preferences = CurrentReaderPreferences();
  view_->AppendThemeStyleSheet(MarkdownTheme::Light,
                               ReaderAppearanceCss(preferences, prototype));
  view_->AppendThemeStyleSheet(MarkdownTheme::Dark,
                               ReaderAppearanceCss(preferences, prototype));
}

void MdViewerWindow::ShowReaderAppearance() {
  if (appearance_popup_ == nullptr) {
    return;
  }
  appearance_popup_->SetPreferences(CurrentReaderPreferences());
  appearance_popup_->OpenNear(appearance_button_);
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
  fluent_frame_->titleBar()->setTitleVisible(window_width >= 660);
  if (outline_dock_ != nullptr && !focus_mode_) {
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

void MdViewerWindow::SetFocusMode(bool enabled) {
  if (document_dock_ == nullptr || outline_dock_ == nullptr ||
      focus_mode_ == enabled) {
    return;
  }
  focus_mode_ = enabled;
  if (enabled) {
    focus_restore_document_ = !document_dock_->isHidden();
    focus_restore_outline_ = !outline_dock_->isHidden();
    document_dock_->hide();
    outline_dock_->hide();
    return;
  }
  if (focus_restore_document_) {
    document_dock_->show();
  }
  if (focus_restore_outline_) {
    outline_dock_->show();
  }
  UpdateChromeVisibility();
}

bool MdViewerWindow::ShowFile(const QString& path, OpenReason reason) {
  const QFileInfo info(path);
  const QString absolute_path = info.absoluteFilePath();
  if (pending_search_hit_.has_value() &&
      pending_search_hit_->path != absolute_path) {
    pending_search_hit_.reset();
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
  if (!view_->current_file().isEmpty() &&
      view_->current_file() != absolute_path) {
    RememberCurrentPosition();
  }
  pending_restore_path_ = absolute_path;
  pending_restore_scroll_ = positions_.ScrollFor(absolute_path);
  if (!view_->LoadFile(absolute_path)) {
    pending_restore_scroll_ = -1;
    pending_restore_path_.clear();
    return false;  // 视图里已经是错误页，状态栏的消息由 loadFailed 负责
  }
  // 旧文档还在屏幕上，而新文档已在后台渲染；先给一个明确的等待页
  view_->ShowPlaceholder(QStringLiteral("正在渲染…"));
  displayed_file_ = absolute_path;
  // 标题栏标签跟 windowTitle 走：根目录 / 相对路径，和原型的面包屑一致
  setWindowTitle(DocumentDisplayTitle(root_dir_, absolute_path));
  if (breadcrumb_ != nullptr) {
    breadcrumb_->setText(DocumentDisplayTitle(root_dir_, absolute_path));
    breadcrumb_->show();
  }
  // 状态栏放完整路径；过长时中间省略，文件名仍可见
  const QString status_path = QDir::toNativeSeparators(absolute_path);
  path_label_->setText(QFontMetrics(path_label_->font())
                           .elidedText(status_path, Qt::ElideMiddle, 520));
  path_label_->setToolTip(status_path);
  documents_panel_->SetCurrentFile(absolute_path);
  UpdateBookmarkUi();
  settings_->last_file = absolute_path;
  settings_->SaveSession();
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
      !displayed_file_.isEmpty() && bookmarks_.Contains(displayed_file_);
  if (star_button_ != nullptr) {
    const QSignalBlocker blocker(star_button_);
    star_button_->setChecked(starred);
    star_button_->setIcon(TextActionIcon(
        starred ? QStringLiteral("★") : QStringLiteral("☆"), starred));
    star_button_->setToolTip(starred
                                 ? QStringLiteral("取消收藏（Ctrl+D）")
                                 : QStringLiteral("收藏当前文档（Ctrl+D）"));
  }
  if (documents_panel_ != nullptr) {
    documents_panel_->SetBookmarks(bookmarks_.items(), root_dir_);
  }
}

void MdViewerWindow::SetBookmarkChecked(bool starred) {
  const QString path = displayed_file_;
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
  if (!ShowFile(absolute, OpenReason::kUser)) {
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
  RememberCurrentPosition();
  settings_->window_geometry = saveGeometry();
  settings_->dock_state = saveState();
  settings_->dark_theme = view_->theme() == MarkdownTheme::Dark;
  settings_->code_line_numbers = view_->code_line_numbers();
  settings_->formula_rendering = view_->formula_rendering_enabled();
  settings_->mermaid_rendering = view_->mermaid_rendering_enabled();
  settings_->persistent_cache = view_->persistent_render_cache_enabled();
  settings_->window_material = window_material_;
  settings_->root_dir = root_dir_;
  settings_->Save();
}

void MdViewerWindow::UpdateZoomLabel(qreal factor) {
  zoom_label_->setText(QStringLiteral("%1%").arg(qRound(factor * 100.0)));
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
  if (hit.file_path == view_->current_file()) {
    // 当前文档正在重渲染：等 renderFinished 再跳，别取消这次渲染
    if (view_->is_rendering() && view_->pending_file() == hit.file_path) {
      pending_search_hit_ = PendingJump{hit.file_path, hit.file_ordinal, query};
      return;
    }
    // 后台正在换到别的文档：取消换篇，留在当前文档里跳
    if (view_->is_rendering() && !view_->pending_file().isEmpty()) {
      view_->CancelPendingRender();
      pending_fragment_.clear();
      displayed_file_ = view_->current_file();
      UpdateBookmarkUi();
    }
    pending_search_hit_.reset();
    JumpToSearchHit(hit.file_ordinal, query);
    return;
  }
  pending_search_hit_ = PendingJump{hit.file_path, hit.file_ordinal, query};
  NavigateTo(hit.file_path, QString(), /*record_history=*/true);
  // 搜索跳转自己管滚动，别让阅读位置恢复把选区顶掉
  pending_restore_scroll_ = -1;
  pending_restore_path_.clear();
}

void MdViewerWindow::JumpToSearchHit(int ordinal, const QString& query) {
  if (query.isEmpty()) {
    return;
  }
  // 从顶部开始数，序号才和源码命中顺序大致对齐
  view_->ClearSelection();
  view_->SetScrollPosition(0);
  bool wrapped = false;
  for (int i = 0; i <= ordinal; ++i) {
    if (!view_->FindText(query, MarkdownView::FindNoFlag, &wrapped,
                         /*incremental=*/false)) {
      statusBar()->showMessage(
          QStringLiteral("无法精确定位该命中，已停在文档顶部"), 4000);
      return;
    }
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
