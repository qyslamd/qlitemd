#include "markdown_view.h"

#include <limits>

#include <QBuffer>
#include <QClipboard>
#include <QColor>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QGuiApplication>
#include <QImage>
#include <QImageReader>
#include <QKeyEvent>
#include <QKeySequence>
#include <QPalette>
#include <QPointer>
#include <QRegularExpression>
#include <QSaveFile>
#include <QScrollBar>
#include <QShortcut>
#include <QTimer>
#include <QUuid>
#include <QVBoxLayout>
#include <QWheelEvent>

#include "code_highlighter.h"
#include "formula_renderer.h"
#include "html_escape.h"
#include "markdown_render_context.h"
#include "markdown_render_context_internal.h"
#include "markdown_theme.h"
#include "markdown_to_html.h"
#include "markdown_types.h"
#include "mermaid_renderer.h"
#include "qlitehtmlwidget.h"
#include "render_cache.h"
#include "render_diagnostics.h"
#include "render_service.h"

namespace {

// "先删后建"保存的等待节奏：每 100ms 看一次，最多等 5 秒
constexpr int kReloadIntervalMs = 100;
constexpr int kReloadMaxAttempts = 50;

// 缩放：步进 1.1 倍，范围与上游示例一致；再大中文会糊、再小认不出字
constexpr qreal kZoomStep = 1.1;
constexpr qreal kZoomMin = 0.3;
constexpr qreal kZoomMax = 5.0;
// 连续缩放停止多久后应用最后一档：太短会把每格滚轮都排一次
constexpr int kZoomApplyDelayMs = 150;

// qrc 读不出来是构建问题、运行时几乎不会发生，但"样式全丢"比"丑一点"
// 严重得多（表格/代码块/图都会失去排版），退回一份最小样式并留痕
QString LoadCssResource(const QString& path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    qWarning() << "markdown: theme css missing from resources:" << path;
    return QStringLiteral(
        "body { font-family: sans-serif; font-size: 15px;"
        " line-height: 1.6; margin: 0; }\n"
        ".md-page { max-width: 980px; margin: 0 auto; padding: 24px 48px; }\n"
        "pre, code { font-family: monospace; }\n"
        "pre { background: #f6f8fa; padding: 8px; overflow-x: auto; }\n"
        "table { border-collapse: collapse; }\n"
        "table th, table td { border: 1px solid #d0d7de;"
        " padding: 4px 8px; }\n"
        "img { max-width: 100%; }\n"
        ".math-display { display: block; margin: 10px auto; }\n"
        ".mermaid-wrap { text-align: center; margin: 12px 0; }\n"
        "img.mermaid { max-width: 100%; height: auto; }\n");
  }
  return QString::fromUtf8(file.readAll());
}

// qrc 里的主题样式，每个主题只读一次（qrc 的内容不会变）
const QString& DefaultCssFromResources(MarkdownTheme theme) {
  static const QString kCss[kMarkdownThemeCount] = {
      LoadCssResource(ThemeCssResource(MarkdownTheme::Light)),
      LoadCssResource(ThemeCssResource(MarkdownTheme::Dark)),
  };
  return kCss[ThemeIndex(theme)];
}

QString RenderIdentity(MarkdownRenderKind kind, const QString& key) {
  return QString::number(static_cast<int>(kind)) + QLatin1Char('\n') + key;
}

QUrl BaseUrlForDir(const QString& base_dir) {
  if (base_dir.isEmpty()) {
    return QUrl();
  }
  QUrl base = QUrl::fromLocalFile(base_dir);
  base.setPath(base.path() + QLatin1Char('/'));
  return base;
}

QUrl ResolveImageUrl(const QString& src, const QString& base_dir) {
  const QString unescaped = UnescapeHtml(src);
  const QUrl url = QUrl::fromEncoded(unescaped.toUtf8(), QUrl::TolerantMode);
  if (url.scheme().isEmpty() || url.isRelative()) {
    return BaseUrlForDir(base_dir).resolved(url);
  }
  return url;
}

QByteArray PixmapToPng(const QPixmap& pixmap) {
  if (pixmap.isNull()) {
    return {};
  }
  QByteArray bytes;
  QBuffer buffer(&bytes);
  if (!buffer.open(QIODevice::WriteOnly) || !pixmap.save(&buffer, "PNG")) {
    return {};
  }
  return bytes;
}

QByteArray DecodeDataUriImage(const QUrl& url) {
  const QString text = url.toString();
  if (!text.startsWith(QLatin1String("data:"), Qt::CaseInsensitive)) {
    return {};
  }
  const int comma = text.indexOf(QLatin1Char(','));
  if (comma < 0) {
    return {};
  }
  const QString meta = text.mid(5, comma - 5);
  const QString payload = text.mid(comma + 1);
  QByteArray raw;
  if (meta.contains(QLatin1String(";base64"), Qt::CaseInsensitive)) {
    raw = QByteArray::fromBase64(payload.toLatin1());
  } else {
    raw = QUrl::fromPercentEncoding(payload.toUtf8()).toUtf8();
  }
  if (raw.isEmpty()) {
    return {};
  }
  QImage image;
  if (!image.loadFromData(raw)) {
    return {};
  }
  QByteArray png;
  QBuffer buffer(&png);
  if (!buffer.open(QIODevice::WriteOnly) || !image.save(&buffer, "PNG")) {
    return {};
  }
  return png;
}

QString ExportImageExtension(const QUrl& url) {
  QString suffix = QFileInfo(url.toLocalFile()).suffix().toLower();
  static const QRegularExpression kSafeSuffix(QStringLiteral("^[a-z0-9]+$"));
  if (!kSafeSuffix.match(suffix).hasMatch()) {
    suffix = QStringLiteral("png");
  }
  return suffix;
}

QString CssOverridePath() {
  static const QString kPath = qEnvironmentVariable("MD_MARKDOWN_CSS");
  return kPath;
}

// 覆盖生效时主题只切公式/Mermaid，CSS 不动；非 Light 主题会静默错位，
// 所以切换时提示一次（每个进程一次，别刷屏）
void WarnThemeCssMismatch() {
  static bool warned = false;
  if (warned) {
    return;
  }
  warned = true;
  qWarning() << "markdown: MD_MARKDOWN_CSS overrides theme css;"
                " SetTheme only changes formula/mermaid colors";
}

// MD_MARKDOWN_CSS=<文件>：存在就覆盖 qrc 里的样式——调样式不用重编，发布
// 默认关闭（与 MD_MERMAID_DUMP 同一个模式）。环境变量读一次；**文件**按
// 变更时间缓存——没变不读盘（每次 ApplyDocument 现读是白读），变了立即
// 跟上，"改完重开文档就生效"的调样式工作流不破坏。设了但读不了就落回 qrc。
// 覆盖生效时 theme 参数被忽略：只换 CSS，不换公式/Mermaid 配色
QString LoadDefaultCss(MarkdownTheme theme) {
  const QString override_path = CssOverridePath();
  if (!override_path.isEmpty()) {
    static QString cached_css;
    static QDateTime cached_modified;
    static qint64 cached_size = -1;
    static bool have_cached = false;
    const QFileInfo info(override_path);
    const QDateTime modified = info.lastModified();
    if (!have_cached || modified != cached_modified ||
        info.size() != cached_size) {
      QFile file(override_path);
      if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "markdown: MD_MARKDOWN_CSS set but unreadable,"
                   << override_path;
        // 失败态不缓存，文件回来了直接用
        return DefaultCssFromResources(theme);
      }
      cached_css = QString::fromUtf8(file.readAll());
      cached_modified = modified;
      cached_size = info.size();
      have_cached = true;
    }
    return cached_css;
  }
  return DefaultCssFromResources(theme);
}

QString WrapDocument(const QString& body, const QString& css) {
  // 正文包一层 .md-page：限宽/居中/内边距都在这一层做。qlitehtml 里 body
  // 的尺寸恒等于视口（container_qpainter.cpp 的 hit-test 注释），body 上写
  // max-width 与自动边距不可靠，必须用普通 div
  return QStringLiteral(
             "<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
             "<style>%1</style></head><body>"
             "<div class=\"md-page\">%2</div></body></html>")
      .arg(css, body);
}

bool ReadMarkdownFile(const QString& path, QString* text) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return false;
  }
  *text = QString::fromUtf8(file.readAll());
  return true;
}

}  // namespace

QString MakeImageToken(const QString& prefix, const QString& cache_key) {
  const QByteArray digest =
      QCryptographicHash::hash(cache_key.toUtf8(), QCryptographicHash::Sha256)
          .toHex();
  return QStringLiteral("mj:") + prefix + QString::fromLatin1(digest);
}

// 驻留形态 → 待提交形态。PNG 解码失败时 image 为空，产物整体按失败处理；
// 这是缓存命中路径唯一的一次 PNG 解码，fresh 渲染路径不经过这里
RenderedImage DecodeCachedImage(const CachedImage& cached) {
  RenderedImage out;
  if (cached.isNull()) {
    return out;
  }
  out.image = QImage::fromData(cached.png_bytes);
  out.png_bytes = cached.png_bytes;
  out.layout_width = cached.layout_width;
  out.layout_height = cached.layout_height;
  return out;
}

// 实现见头文件注释（两道闸）。留在自由函数里是为了可测：资源回调是
// qlitehtml 的私有路径，单测从这层进去就够了
QByteArray ReadResourceForUrl(const QUrl& url, const QString& base_dir) {
  // 公式图是 data URI 内联进来的（formula_renderer 产出），这里解码还原。
  // 用 data URI 而不是自定义 scheme，是因为后者会被 qlitehtml 当主机名做
  // 浏览器式扩展（实测 mj://0 → mj://0.0.0.0），回调里对不上号
  static const QString kPngPrefix = QStringLiteral("data:image/png;base64,");
  const QString text = url.toString();
  if (text.startsWith(kPngPrefix)) {
    // 编码后长度先卡一刀：解码后再量就晚了（内存已经吃进去）
    constexpr int kMaxDecodedBytes = 16 * 1024 * 1024;
    const int encoded_len = text.size() - kPngPrefix.size();
    if (encoded_len > (kMaxDecodedBytes / 3 + 1) * 4) {
      qWarning() << "markdown: data URI exceeds the 16MB decode limit";
      return {};
    }
    return QByteArray::fromBase64(text.mid(kPngPrefix.size()).toLatin1());
  }

  if (!url.isLocalFile() || base_dir.isEmpty()) {
    return {};
  }
  const QString base = QFileInfo(base_dir).canonicalFilePath();
  if (base.isEmpty()) {
    return {};
  }
  const QString path = QFileInfo(url.toLocalFile()).canonicalFilePath();
  if (path.isEmpty()) {
    return {};
  }
#ifdef Q_OS_WIN
  const Qt::CaseSensitivity kCase = Qt::CaseInsensitive;
#else
  const Qt::CaseSensitivity kCase = Qt::CaseSensitive;
#endif
  if (path.compare(base, kCase) != 0 &&
      !path.startsWith(base + QLatin1Char('/'), kCase)) {
    qWarning() << "markdown: local resource outside the document dir, rejected:"
               << path;
    return {};
  }
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    return {};
  }
  return file.readAll();
}

MarkdownView::MarkdownView(QWidget* parent)
    : QWidget(parent),
      context_(new MarkdownRenderContext),
      view_id_(QUuid::createUuid().toString(QUuid::WithoutBraces)) {
  initUi();
  initSignals();
}

QString CopyTextForLink(const QUrl& url,
                        const QHash<QString, QString>& blocks) {
  if (url.scheme() != QLatin1String("copy")) {
    return QString();
  }
  return blocks.value(url.path());
}

// 析构写在 .cpp：成员里只有第三方控件的前向声明，需要完整类型才能生成
MarkdownView::~MarkdownView() {
  if (render_service_ != nullptr) {
    render_service_->CancelView(view_id_);
  }
}

void MarkdownView::initUi() {
  setObjectName(QStringLiteral("markdownView"));

  html_ = new QLiteHtmlWidget(this);
  html_->setObjectName(QStringLiteral("markdownHtml"));
  html_->setResourceHandler([this](const QUrl& url) {
    // provider 未接住的 token 不当作资源加载失败，避免误报
    if (url.scheme() == QLatin1String("mj")) {
      return QByteArray();
    }
    const QByteArray data = ReadResourceForUrl(url, committed_base_dir_);
    if (data.isEmpty() && url.scheme() != QLatin1String("data")) {
      emit resourceLoadFailed(url);
    }
    const MarkdownDiagnostics diagnostics = context_->diagnostics();
    if (diagnostics.trace) {
      // data URI 的字符串就是整张 PNG，日志里不展开
      const QString label = url.scheme() == QLatin1String("data")
                                ? QStringLiteral("data URI")
                                : url.toString();
      TraceRender(
          diagnostics,
          QStringLiteral("resource %1: %2 bytes").arg(label).arg(data.size()));
    }
    return data;
  });
  // 公式/图表图片直供：只处理本模块注册过的 mj: token，其余走 DataCallback
  html_->setPixmapCallBack([this](const QUrl& url) {
    if (url.scheme() != QLatin1String("mj")) {
      return QPixmap();
    }
    const QString token = QString::fromLatin1(url.toEncoded());
    const auto it = document_pixmaps_.constFind(token);
    if (it == document_pixmaps_.constEnd()) {
      qWarning() << "markdown: pixmap provider miss for" << url;
      return QPixmap();
    }
    return it->pixmap;
  });
  // 缩放快捷键与 Ctrl+C 要做成控件的内置能力：Wheel 发到 viewport、
  // KeyPress 发到 widget 本身，两处都装（见 eventFilter）
  html_->setFocusPolicy(Qt::StrongFocus);
  html_->installEventFilter(this);
  html_->viewport()->installEventFilter(this);

  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->addWidget(html_);
}

void MarkdownView::initSignals() {
  // copy: 是代码块的"复制"链接（渲染层产出），在这里就地处理、不往宿主抛：
  // 它不是导航请求，宿主不需要知道
  connect(html_, &QLiteHtmlWidget::linkClicked, this, [this](const QUrl& url) {
    if (url.scheme() == QLatin1String("copy")) {
      const QString text = CopyTextForLink(url, code_blocks_);
      if (!text.isEmpty()) {
        QGuiApplication::clipboard()->setText(text);
        emit codeCopied();
      }
      return;
    }
    emit linkClicked(url);
  });
  connect(html_, &QLiteHtmlWidget::imageClicked, this,
          &MarkdownView::imageClicked);
  connect(html_, &QLiteHtmlWidget::imageContextMenuRequested, this,
          &MarkdownView::imageContextMenuRequested);

  // 滚动联动只报位置，不在这里反查章节：策略归宿主（见 outline()）
  connect(html_->verticalScrollBar(), &QScrollBar::valueChanged, this,
          &MarkdownView::viewportScrolled);

  watcher_ = new QFileSystemWatcher(this);
  connect(watcher_, &QFileSystemWatcher::fileChanged, this,
          [this](const QString&) { OnFileChanged(); });

  // 缩放合并：只在停手后排最后一档
  zoom_timer_ = new QTimer(this);
  zoom_timer_->setSingleShot(true);
  zoom_timer_->setInterval(kZoomApplyDelayMs);
  connect(zoom_timer_, &QTimer::timeout, this, &MarkdownView::ApplyPendingZoom);

  // 不是 singleShot：文件被删后要持续等它回来，见 OnFileChanged
  reload_timer_ = new QTimer(this);
  reload_timer_->setInterval(kReloadIntervalMs);
  connect(reload_timer_, &QTimer::timeout, this, &MarkdownView::OnReloadTick);

  // 缩放的键盘快捷键走 QShortcut（WindowShortcut）：eventFilter 那条路要求
  // 焦点先在预览上，而查看窗口刚打开时焦点不在（实测 Ctrl+= 不生效），
  // "窗口激活就能按"才是查看器该有的行为
  auto add_zoom_shortcut = [this](const QKeySequence& keys, qreal factor,
                                  bool reset) {
    auto* shortcut = new QShortcut(keys, this);
    shortcut->setContext(Qt::WindowShortcut);
    connect(shortcut, &QShortcut::activated, this,
            [this, factor, reset] { SetZoom(reset ? 1.0 : zoom_ * factor); });
  };
  add_zoom_shortcut(QKeySequence::ZoomIn, kZoomStep, false);
  add_zoom_shortcut(QKeySequence::ZoomOut, 1.0 / kZoomStep, false);
  add_zoom_shortcut(QKeySequence(QStringLiteral("Ctrl+=")), kZoomStep, false);
  add_zoom_shortcut(QKeySequence(QStringLiteral("Ctrl+-")), 1.0 / kZoomStep,
                    false);
  add_zoom_shortcut(QKeySequence(QStringLiteral("Ctrl+0")), 1.0, true);
}

bool MarkdownView::eventFilter(QObject* watched, QEvent* event) {
  const bool from_html = watched == html_ || watched == html_->viewport();
  if (from_html && event->type() == QEvent::Wheel) {
    auto* wheel = static_cast<QWheelEvent*>(event);
    if (wheel->modifiers().testFlag(Qt::ControlModifier)) {
      const int delta = wheel->angleDelta().y();
      if (delta != 0) {
        SetZoom(zoom_ * (delta > 0 ? kZoomStep : 1.0 / kZoomStep));
        return true;  // 拦下：不拦会同时滚页面
      }
    }
  }
  if (from_html && event->type() == QEvent::KeyPress) {
    auto* key = static_cast<QKeyEvent*>(event);
    // QAbstractScrollArea 的默认按键处理会把 Copy 吞掉，选中文本复制不了。
    // 键盘缩放不在这里做——那条路要求焦点先在预览上，走 QShortcut（见
    // initSignals）
    if (key->matches(QKeySequence::Copy)) {
      const QString selected = html_->selectedText();
      if (!selected.isEmpty()) {
        QGuiApplication::clipboard()->setText(selected);
        return true;
      }
    }
  }
  return QWidget::eventFilter(watched, event);
}

void MarkdownView::SetZoom(qreal factor) {
  const qreal next = qBound(kZoomMin, factor, kZoomMax);
  if (qFuzzyCompare(next, zoom_)) {
    return;
  }
  // 先改对外状态，连续按键不丢档；控件与重排交给定时器合并
  zoom_ = next;
  emit zoomChanged(zoom_);
  zoom_timer_->start();
}

void MarkdownView::SetZoomFactor(qreal factor) { SetZoom(factor); }

void MarkdownView::ZoomIn() { SetZoom(zoom_ * kZoomStep); }

void MarkdownView::ZoomOut() { SetZoom(zoom_ / kZoomStep); }

void MarkdownView::ResetZoom() { SetZoom(1.0); }

int MarkdownView::ScrollPosition() const {
  return html_->verticalScrollBar()->value();
}

void MarkdownView::SetScrollPosition(int y) {
  // 挂起的缩放会重排，滚动值随之漂移；先落地再设
  FlushPendingZoom();
  html_->verticalScrollBar()->setValue(y);
}

int MarkdownView::DocumentHeight() const {
  return html_ != nullptr ? html_->documentHeight() : 0;
}

int MarkdownView::ViewportHeight() const {
  if (html_ == nullptr) {
    return 0;
  }
  // 滚动条与 DocumentHeight 都是虚拟像素：视口高度要除掉缩放，
  // 否则阅读进度在非 100% 缩放下分母偏大/偏小。
  const qreal zoom = html_->zoomFactor();
  if (zoom <= 0.0) {
    return 0;
  }
  return qRound(html_->viewport()->height() / zoom);
}

int MarkdownView::layout_count() const { return html_->layoutCount(); }

void MarkdownView::ApplyPendingZoom() {
  if (qFuzzyCompare(html_->zoomFactor(), zoom_)) {
    return;
  }
  html_->setZoomFactor(zoom_);
  // 锚点缓存不在这里失效：布局代数变了，下次 AnchorY 查询自然会重填
}

void MarkdownView::FlushPendingZoom() {
  zoom_timer_->stop();
  ApplyPendingZoom();
}

bool MarkdownView::LoadFile(const QString& path) {
  const bool notify_reload = reload_notify_on_load_;
  reload_notify_on_load_ = false;
  QString text;
  if (!ReadMarkdownFile(path, &text)) {
    ClearDocument();
    ShowMessage(QStringLiteral("无法打开文件：%1").arg(path));
    emit loadFailed(path, QStringLiteral("无法打开文件"));
    return false;
  }

  // 换文件才丢缓存：同一文件的重载（watchdog）要留住复用价值；SetContent
  // 是编辑器路径，每次清会把"改一个字重算全部公式"的代价请回来——那条路由
  // 宿主自己按需调 ClearRenderCaches()（或
  // render_context().ClearMemoryCaches()）
  if (path != current_file_) {
    context_->ClearMemoryCaches();
  }
  markdown_ = text;
  current_file_ = path;
  base_dir_ = QFileInfo(path).absolutePath();
  WatchCurrentFile();
  ApplyDocument(false, notify_reload);
  return true;
}

void MarkdownView::SetContent(const QString& markdown,
                              const QString& base_dir) {
  ClearDocument();  // 先清空（它会清 markdown_/base_dir_），再落新内容
  markdown_ = markdown;
  base_dir_ = base_dir;
  ApplyDocument();
}

void MarkdownView::ShowPlaceholder(const QString& text) {
  // 只在第一次换成占位页时记滚动位置；后续调用读到的已经是占位页的 0
  if (!placeholder_visible_) {
    placeholder_scroll_ = html_->verticalScrollBar()->value();
  }
  placeholder_visible_ = true;
  zoom_timer_->stop();
  html_->setZoomFactorWithoutRender(zoom_);
  html_->setUrl(BaseUrlForDir(base_dir_));
  // qlitehtml 的 vh 在 padding/height 里会退化成裸数值，这里按视口高度
  // 算像素。行盒撑满内容区，单行文字自然垂直居中；高度也把主题背景铺满
  constexpr int kPagePadding = 24;
  const int viewport_height = html_->viewport()->height();
  const int content_height = qMax(0, viewport_height - 2 * kPagePadding);
  html_->setHtml(
      WrapDocument(QStringLiteral("<div style=\"height:%1px; line-height:%2px;"
                                  " text-align:center;\">%3</div>")
                       .arg(content_height)
                       .arg(content_height)
                       .arg(text.toHtmlEscaped()),
                   CssForTheme(theme_)));
}

void MarkdownView::ScrollToAnchor(const QString& anchor) {
  if (anchor.isEmpty()) {
    return;
  }
  // 挂起的缩放不落地，anchorY 会按旧布局算、按旧 maximum 钳位
  FlushPendingZoom();
  // 用户写的锚点是 slug（可能是 CJK），元素的 id 是纯 ASCII 的 h-N——查表
  // 换过去（表在 ApplyDocument 里建好）。查不到就原样透传：手写的
  // <a name="x"> 这类锚点还得靠 litehtml 自己找
  html_->scrollToAnchor(heading_ids_.value(anchor, anchor));
}

std::optional<int> MarkdownView::AnchorY(const QString& anchor) const {
  if (heading_ids_.constFind(anchor) == heading_ids_.constEnd()) {
    return std::nullopt;
  }
  // 用布局代数判新鲜度：纯高度 resize 不重排、代数不变，缓存可以继续用；
  // 宽度/缩放/换文档引起重排后代数变化，查询时自然重填。不依赖信号时序
  const int layout_count = html_->layoutCount();
  if (anchor_y_cache_dirty_ || anchor_y_layout_count_ != layout_count) {
    // 一次性查完整张大表：宿主按滚动位置反查章节时是每帧遍历，逐次下探
    // litehtml 会把 O(n) 次 DOM 搜索压在滚动路径上
    anchor_y_cache_.clear();
    for (const MarkdownHeading& heading : outline_) {
      const auto id = heading_ids_.constFind(heading.anchor);
      if (id == heading_ids_.constEnd()) {
        continue;
      }
      const int y = html_->anchorY(id.value());
      if (y >= 0) {
        anchor_y_cache_.insert(heading.anchor, y);
      }
    }
    anchor_y_layout_count_ = layout_count;
    // 布局尚未发生（全都没查到）时保持 dirty，下一次再试
    anchor_y_cache_dirty_ = !outline_.isEmpty() && anchor_y_cache_.isEmpty();
  }
  const auto cached = anchor_y_cache_.constFind(anchor);
  if (cached == anchor_y_cache_.constEnd()) {
    return std::nullopt;
  }
  return cached.value();
}

void MarkdownView::InvalidateAnchorYCache() {
  anchor_y_cache_.clear();
  anchor_y_cache_dirty_ = true;
}

bool MarkdownView::FindText(const QString& text, FindFlags flags, bool* wrapped,
                            bool incremental) {
  if (text.isEmpty()) {
    return false;  // 空串不是"找全部"，是误用；选区与滚动都别动
  }
  // 未落地的缩放会在 150ms 后重排，命中位置随之漂移
  FlushPendingZoom();
  // 控件只在"绕回"时写 wrapped，没绕回就原样留着——宿主拿到的会是个没初始化的
  // 值。这里先清零，把契约补成"每次调用都有效"
  if (wrapped != nullptr) {
    *wrapped = false;
  }
  QTextDocument::FindFlags qt_flags;
  if (flags & FindFlag::FindBackward) {
    qt_flags |= QTextDocument::FindBackward;
  }
  if (flags & FindFlag::FindCaseSensitively) {
    qt_flags |= QTextDocument::FindCaseSensitively;
  }
  if (flags & FindFlag::FindWholeWords) {
    qt_flags |= QTextDocument::FindWholeWords;
  }
  return html_->findText(text, qt_flags, incremental, wrapped);
}

void MarkdownView::ClearSelection() { html_->clearSelection(); }

bool MarkdownView::has_selection() const {
  return !html_->selectedText().isEmpty();
}

bool MarkdownView::ExportHtml(const QString& html_path, QString* error) const {
  auto fail = [error](const QString& message) {
    if (error != nullptr) {
      *error = message;
    }
    return false;
  };
  if (rendering_) {
    return fail(QStringLiteral("文档还在渲染"));
  }
  if (!has_committed_ || committed_html_.isEmpty()) {
    return fail(QStringLiteral("没有可导出的文档"));
  }
  if (html_path.isEmpty()) {
    return fail(QStringLiteral("导出路径为空"));
  }

  const QFileInfo html_info(html_path);
  const QDir target_dir = html_info.absoluteDir();
  const QString base_name = html_info.completeBaseName().isEmpty()
                                ? QStringLiteral("document")
                                : html_info.completeBaseName();
  const QString asset_dir_name = base_name + QStringLiteral("_files");
  const QString asset_dir_path = target_dir.filePath(asset_dir_name);
  const bool asset_dir_existed = QDir(asset_dir_path).exists();
  bool asset_dir_created = false;
  bool fatal_asset = false;
  QHash<QByteArray, QString> written;
  QHash<QString, QString> resolved;

  auto fail_cleaning = [&](const QString& message) {
    if (asset_dir_created && !asset_dir_existed) {
      QDir(asset_dir_path).removeRecursively();
    }
    return fail(message);
  };

  auto write_asset = [&](const QByteArray& bytes, const QString& prefix,
                         const QString& extension) -> QString {
    if (bytes.isEmpty()) {
      return QString();
    }
    const QByteArray digest =
        QCryptographicHash::hash(bytes, QCryptographicHash::Sha256)
            .toHex()
            .left(16);
    if (const auto it = written.constFind(digest); it != written.constEnd()) {
      return it.value();
    }
    const QString file_name =
        prefix + QString::fromLatin1(digest) + QLatin1Char('.') + extension;
    if (!asset_dir_created) {
      if (!QDir().mkpath(asset_dir_path)) {
        return QString();
      }
      asset_dir_created = true;
    }
    QFile file(QDir(asset_dir_path).filePath(file_name));
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size()) {
      return QString();
    }
    const QString relative =
        QString::fromLatin1(QUrl::toPercentEncoding(asset_dir_name)) +
        QLatin1Char('/') + file_name;
    written.insert(digest, relative);
    return relative;
  };

  const auto relative_for_src = [&](const QString& src) -> QString {
    const auto cached = resolved.constFind(src);
    if (cached != resolved.constEnd()) {
      return cached.value();
    }
    QString relative;
    const QUrl url = ResolveImageUrl(src, committed_base_dir_);
    if (url.scheme() == QLatin1String("mj")) {
      const QString token = QString::fromLatin1(url.toEncoded());
      const PinnedImage pinned = document_pixmaps_.value(token);
      // 新渲图走 worker 编的原始 PNG 直写；跨代缓存命中图无 png，
      // 退回 QPixmap 重编码（与旧行为一致）
      const QByteArray png = !pinned.png_bytes.isEmpty()
                                 ? pinned.png_bytes
                                 : PixmapToPng(pinned.pixmap);
      if (png.isEmpty()) {
        fatal_asset = true;
        return QString();
      }
      const bool formula = token.startsWith(QLatin1String("mj:f/"));
      relative = write_asset(
          png,
          formula ? QStringLiteral("formula-") : QStringLiteral("mermaid-"),
          QStringLiteral("png"));
      if (relative.isEmpty()) {
        fatal_asset = true;
        return QString();
      }
    } else if (url.scheme() == QLatin1String("data")) {
      const QByteArray png = DecodeDataUriImage(url);
      if (!png.isEmpty()) {
        relative =
            write_asset(png, QStringLiteral("image-"), QStringLiteral("png"));
      }
    } else if (url.isLocalFile()) {
      const QByteArray bytes = ReadResourceForUrl(url, committed_base_dir_);
      if (!bytes.isEmpty()) {
        relative = write_asset(bytes, QStringLiteral("image-"),
                               ExportImageExtension(url));
      }
    }
    resolved.insert(src, relative);
    return relative;
  };

  static const QRegularExpression kImgSrc(
      QStringLiteral("(<img\\b[^>]*?\\bsrc\\s*=\\s*)([\"'])(.*?)(\\2)"),
      QRegularExpression::CaseInsensitiveOption |
          QRegularExpression::DotMatchesEverythingOption);
  QString output;
  output.reserve(committed_html_.size());
  int last = 0;
  QRegularExpressionMatchIterator matches =
      kImgSrc.globalMatch(committed_html_);
  while (matches.hasNext()) {
    const QRegularExpressionMatch match = matches.next();
    output += committed_html_.mid(last, match.capturedStart() - last);
    const QString src = match.captured(3);
    const QString relative = relative_for_src(src);
    if (fatal_asset) {
      return fail_cleaning(QStringLiteral("公式/Mermaid 图片写入失败"));
    }
    if (relative.isEmpty()) {
      output += match.captured(0);
    } else {
      output += match.captured(1) + match.captured(2) +
                relative.toHtmlEscaped() + match.captured(4);
    }
    last = match.capturedEnd();
  }
  output += committed_html_.mid(last);

  QSaveFile file(html_path);
  const QByteArray utf8 = output.toUtf8();
  if (!file.open(QIODevice::WriteOnly) || file.write(utf8) != utf8.size() ||
      !file.commit()) {
    return fail_cleaning(QStringLiteral("写入 HTML 失败"));
  }
  return true;
}

QPixmap MarkdownView::ImagePixmap(const QUrl& url) const {
  if (url.scheme() == QLatin1String("mj")) {
    return document_pixmaps_.value(QString::fromLatin1(url.toEncoded())).pixmap;
  }
  QByteArray bytes;
  if (url.scheme() == QLatin1String("data")) {
    bytes = DecodeDataUriImage(url);
  } else if (url.isLocalFile()) {
    bytes = ReadResourceForUrl(url, committed_base_dir_);
  }
  if (bytes.isEmpty()) {
    return QPixmap();
  }
  QPixmap pixmap;
  return pixmap.loadFromData(bytes) ? pixmap : QPixmap();
}

std::optional<MarkdownImageInfo> MarkdownView::ImageInfo(
    const QUrl& url) const {
  if (url.scheme() != QLatin1String("mj")) {
    return std::nullopt;
  }
  const auto it = image_info_.constFind(QString::fromLatin1(url.toEncoded()));
  if (it == image_info_.constEnd()) {
    return std::nullopt;
  }
  return it.value();
}

QVector<QUrl> MarkdownView::GeneratedImageUrls() const {
  QVector<QUrl> urls;
  urls.reserve(image_info_.size());
  for (auto it = image_info_.constBegin(); it != image_info_.constEnd(); ++it) {
    urls.append(QUrl(it.key()));
  }
  return urls;
}

void MarkdownView::Print(QPrinter* printer) {
  if (printer == nullptr || rendering_) {
    return;
  }
  html_->print(printer);
}

MarkdownRenderContext& MarkdownView::render_context() { return *context_; }

const MarkdownRenderContext& MarkdownView::render_context() const {
  return *context_;
}

void MarkdownView::ClearRenderCaches() { context_->ClearMemoryCaches(); }

void MarkdownView::SetPersistentRenderCacheEnabled(bool enabled) {
  MarkdownRenderContextOptions options = context_->options();
  if (options.persistent_cache_enabled == enabled) {
    return;
  }
  options.persistent_cache_enabled = enabled;
  context_->SetOptions(options);
}

bool MarkdownView::persistent_render_cache_enabled() const {
  return context_->options().persistent_cache_enabled;
}

void MarkdownView::SetRenderCacheDirectory(const QString& dir) {
  MarkdownRenderContextOptions options = context_->options();
  if (options.cache_directory == dir) {
    return;
  }
  options.cache_directory = dir;
  context_->SetOptions(options);
}

void MarkdownView::ClearPersistentRenderCache() {
  ClearRenderCacheDirectory(MakeRenderCacheRootConfig(context_->options()));
}

void MarkdownView::SetCodeLineNumbers(bool enabled) {
  if (code_line_numbers_ == enabled) {
    return;
  }
  code_line_numbers_ = enabled;
  ReapplyDocument();
}

void MarkdownView::SetFormulaRenderingEnabled(bool enabled) {
  if (formula_rendering_enabled_ == enabled) {
    return;
  }
  formula_rendering_enabled_ = enabled;
  ReapplyDocument();
}

void MarkdownView::SetMermaidRenderingEnabled(bool enabled) {
  if (mermaid_rendering_enabled_ == enabled) {
    return;
  }
  mermaid_rendering_enabled_ = enabled;
  ReapplyDocument();
}

void MarkdownView::SetTheme(MarkdownTheme theme) {
  if (theme_ == theme) {
    return;
  }
  // 宿主按主题提供了完整 CSS 时，环境变量覆盖不参与，不需要 warning
  if (theme != MarkdownTheme::Light && !CssOverridePath().isEmpty() &&
      theme_style_sheets_[ThemeIndex(theme)].isEmpty()) {
    WarnThemeCssMismatch();
  }
  theme_ = theme;
  ReapplyDocument();
}

void MarkdownView::SetThemeStyleSheet(MarkdownTheme theme, const QString& css) {
  const int index = ThemeIndex(theme);
  if (theme_style_sheets_[index] == css) {
    return;
  }
  theme_style_sheets_[index] = css;
  if (theme_ == theme) {
    ReapplyDocument();
  }
}

void MarkdownView::AppendThemeStyleSheet(MarkdownTheme theme,
                                         const QString& css) {
  const int index = ThemeIndex(theme);
  if (theme_style_sheet_appends_[index] == css) {
    return;
  }
  theme_style_sheet_appends_[index] = css;
  if (theme_ == theme) {
    ReapplyDocument();
  }
}

QString MarkdownView::CssForTheme(MarkdownTheme theme) const {
  const QString& custom = theme_style_sheets_[ThemeIndex(theme)];
  QString css = custom.isEmpty() ? LoadDefaultCss(theme) : custom;
  const QString& append = theme_style_sheet_appends_[ThemeIndex(theme)];
  if (append.isEmpty()) {
    return css;
  }
  if (!css.isEmpty() && !css.endsWith(QLatin1Char('\n'))) {
    css += QLatin1Char('\n');
  }
  css += append;
  return css;
}

void MarkdownView::SetPageBackground(const QColor& color) {
  if (page_background_set_ && page_background_ == color) {
    return;
  }
  page_background_ = color;
  page_background_set_ = true;
  ApplyPageBackground();
}

void MarkdownView::ApplyPageBackground() {
  if (html_ == nullptr) {
    return;
  }
  const bool transparent = page_background_.alpha() == 0;
  auto apply = [&](QWidget* widget) {
    if (widget == nullptr) {
      return;
    }
    QPalette palette = widget->palette();
    palette.setColor(QPalette::Base, page_background_);
    palette.setColor(QPalette::Window, page_background_);
    widget->setPalette(palette);
    widget->setAutoFillBackground(!transparent);
    widget->setAttribute(Qt::WA_TranslucentBackground, transparent);
  };
  apply(this);
  apply(html_);
  apply(html_->viewport());
}

MarkdownRendererStatus MarkdownView::renderer_status() const {
  MarkdownRendererStatus status = context_->status();
  status.highlighter = highlight_engine_status_;
  status.formula = formula_engine_status_;
  status.mermaid = mermaid_engine_status_;
  return status;
}

void MarkdownView::ResetRendererStats() {
  context_->ResetStats();
  render_history_ = MarkdownRenderHistory();
}

MarkdownServiceStatus MarkdownView::render_service_status() const {
  if (render_service_ == nullptr) {
    return {};
  }
  return render_service_->Status();
}

void MarkdownView::SetDiagnostics(const MarkdownDiagnostics& diagnostics) {
  context_->SetDiagnostics(diagnostics);
}

MarkdownDiagnostics MarkdownView::diagnostics() const {
  return context_->diagnostics();
}

void MarkdownView::ReapplyDocument() {
  // 首次派发尚未提交时也要能换主题/开关：此时 markdown_ 里是待提交内容
  if (!has_document() && !rendering_) {
    return;
  }
  ApplyDocument(true, false);
}

void MarkdownView::ApplyDocument(bool preserve_scroll,
                                 bool notify_document_reloaded) {
  ++render_history_.requests_started;
  if (active_request_id_ != 0 && render_service_ != nullptr) {
    ++render_history_.requests_cancelled;
    render_service_->Cancel(view_id_, active_request_id_);
  }
  request_clock_.start();
  pending_timings_ = MarkdownRenderTimings();
  const quint64 request_id = ++render_request_id_;
  active_request_id_ = request_id;
  rendering_ = true;
  pending_notify_reload_ = notify_document_reloaded;
  pending_notify_viewport_ = preserve_scroll;
  pending_scroll_ =
      preserve_scroll ? std::optional<int>(html_->verticalScrollBar()->value())
                      : std::nullopt;
  pending_parse_ = MarkdownParseResult();
  pending_rendered_.clear();
  pending_total_ = 0;
  pending_completed_ = 0;
  pending_in_flight_ = 0;
  // 解析/派发放到事件循环，保证 SetContent 返回时宿主已能接 renderFinished
  QTimer::singleShot(0, this, [this, request_id]() {
    if (request_id != active_request_id_) {
      return;
    }
    StartRender(request_id);
  });
}

void MarkdownView::StartRender(quint64 request_id) {
  if (request_id != active_request_id_ || !rendering_) {
    return;
  }
  QElapsedTimer stage_clock;
  stage_clock.start();
  pending_parse_ = ParseMarkdown(markdown_, formula_rendering_enabled_);
  pending_timings_.parse_ms = stage_clock.elapsed();
  const MarkdownDiagnostics diagnostics = context_->diagnostics();
  if (pending_parse_.parse_error) {
    pending_total_ = 0;
    pending_completed_ = 0;
    pending_in_flight_ = 0;
    emit renderProgress(request_id, 0, 0, 0);
    CommitRender(request_id);
    return;
  }

  MarkdownRenderContextState& state =
      MarkdownRenderContextAccess::State(*context_);
  stage_clock.restart();
  QVector<MarkdownRenderTask> tasks =
      CollectRenderTasks(pending_parse_, mermaid_rendering_enabled_,
                         ThemeFormulaColor(theme_), theme_);
  pending_timings_.collect_ms = stage_clock.elapsed();
  pending_total_ = tasks.size();
  pending_completed_ = 0;
  pending_in_flight_ = 0;

  QVector<MarkdownRenderTask> dispatch;
  for (MarkdownRenderTask task : tasks) {
    task.diagnostics = diagnostics;
    const QString identity = RenderIdentity(task.kind, task.cache_key);
    MarkdownContextStatus* stats = nullptr;
    const QString* memory_html = nullptr;
    const CachedImage* memory_image = nullptr;
    if (task.kind == MarkdownRenderKind::Highlight) {
      stats = &state.highlight_stats;
      memory_html = state.highlight_cache.Find(task.cache_key);
    } else if (task.kind == MarkdownRenderKind::Formula) {
      stats = &state.formula_stats;
      memory_image = state.formula_cache.Find(task.cache_key);
    } else {
      stats = &state.mermaid_stats;
      memory_image = state.mermaid_cache.Find(task.cache_key);
    }

    std::variant<QString, RenderedImage> cached;
    bool hit = false;
    if (task.kind == MarkdownRenderKind::Highlight) {
      if (memory_html != nullptr) {
        cached = *memory_html;
        hit = true;
        ++stats->memory_cache_hits;
      } else {
        ++stats->memory_cache_misses;
      }
    } else {
      // 失败产物也缓存（null CachedImage）：同一个坏输入不重试
      if (memory_image != nullptr) {
        hit = true;
        ++stats->memory_cache_hits;
        cached = DecodeCachedImage(*memory_image);
      } else {
        ++stats->memory_cache_misses;
        const QString kind_name = task.kind == MarkdownRenderKind::Formula
                                      ? QStringLiteral("formula")
                                      : QStringLiteral("mermaid");
        const RenderCacheConfig cache_config =
            MakeRenderCacheConfig(state.options, kind_name);
        if (cache_config.enabled) {
          QString disk_key;
          if (task.kind == MarkdownRenderKind::Formula) {
            const QString variant = task.formula_color.isValid()
                                        ? task.formula_color.name()
                                        : QStringLiteral("legacy");
            disk_key = MakeRenderCacheKey(
                kind_name, task.payload.trimmed(),
                (task.display ? QStringLiteral("D") : QStringLiteral("I")) +
                    QLatin1Char('|') + variant);
          } else {
            disk_key = MakeRenderCacheKey(kind_name, task.payload,
                                          MermaidRenderVariant(task.theme));
          }
          CachedImage disk_image;
          if (LoadRenderCacheEntry(cache_config, kind_name, disk_key,
                                   &disk_image)) {
            ++stats->disk_cache_hits;
            hit = true;
            RenderImageCache* memory_cache =
                task.kind == MarkdownRenderKind::Formula ? &state.formula_cache
                                                         : &state.mermaid_cache;
            memory_cache->Insert(task.cache_key, disk_image);
            cached = DecodeCachedImage(disk_image);
          } else {
            ++stats->disk_cache_misses;
          }
        }
      }
    }

    if (hit) {
      pending_rendered_.insert(identity, std::move(cached));
      ++pending_completed_;
    } else {
      ++stats->renders;
      dispatch.append(task);
    }
  }

  pending_in_flight_ = dispatch.size();
  emit renderProgress(request_id, pending_completed_, pending_total_,
                      pending_in_flight_);
  if (dispatch.isEmpty()) {
    CommitRender(request_id);
    return;
  }
  if (render_service_ == nullptr) {
    render_service_ = &MarkdownRenderService::Instance();
    connect(render_service_, &MarkdownRenderService::taskFinished, this,
            [this](const QString& view_id, const MarkdownRenderResult& result) {
              if (view_id == view_id_) {
                OnTaskFinished(result.task.request_id, result);
              }
            });
    connect(render_service_, &MarkdownRenderService::allFinished, this,
            &MarkdownView::OnAllFinished);
  }
  render_service_->Submit(view_id_, request_id, dispatch);
}

void MarkdownView::OnTaskFinished(quint64 request_id,
                                  const MarkdownRenderResult& result) {
  if (request_id != active_request_id_ || !rendering_) {
    ++render_history_.stale_results;
    return;
  }
  pending_timings_.worker_ms += result.elapsed_ms;
  pending_timings_.worker_max_ms =
      qMax(pending_timings_.worker_max_ms, result.elapsed_ms);
  if (result.engine_load_failed) {
    ++render_history_.failed_worker_results;
  }
  MarkdownRenderContextState& state =
      MarkdownRenderContextAccess::State(*context_);
  const QString identity =
      RenderIdentity(result.task.kind, result.task.cache_key);

  if (result.task.kind == MarkdownRenderKind::Highlight) {
    pending_rendered_.insert(identity, result.rendered);
    if (!result.ok) {
      ++state.highlight_stats.failures;
    } else {
      state.highlight_cache.Insert(result.task.cache_key, result.rendered);
    }
    if (result.loaded) {
      highlight_engine_status_.loaded = true;
      if (highlight_engine_status_.first_load_ms == 0) {
        highlight_engine_status_.first_load_ms = result.first_load_ms;
      }
    }
    if (result.engine_all_failed) {
      highlight_engine_status_.permanently_failed = true;
    }
  } else if (result.task.kind == MarkdownRenderKind::Formula) {
    pending_rendered_.insert(identity, result.image);
    if (result.image.isNull()) {
      ++state.formula_stats.failures;
    }
    const CachedImage cached{result.image.png_bytes, result.image.layout_width,
                             result.image.layout_height};
    state.formula_cache.Insert(result.task.cache_key, cached);
    if (!result.image.isNull()) {
      const QString variant = result.task.formula_color.isValid()
                                  ? result.task.formula_color.name()
                                  : QStringLiteral("legacy");
      const QString disk_key = MakeRenderCacheKey(
          QStringLiteral("formula"), result.task.payload.trimmed(),
          (result.task.display ? QStringLiteral("D") : QStringLiteral("I")) +
              QLatin1Char('|') + variant);
      const RenderCacheConfig cache_config =
          MakeRenderCacheConfig(state.options, QStringLiteral("formula"));
      if (cache_config.enabled) {
        StoreRenderCacheEntry(cache_config, QStringLiteral("formula"), disk_key,
                              cached);
      }
    }
    if (result.loaded) {
      formula_engine_status_.loaded = true;
      if (formula_engine_status_.first_load_ms == 0) {
        formula_engine_status_.first_load_ms = result.first_load_ms;
      }
    }
    if (result.engine_all_failed) {
      formula_engine_status_.permanently_failed = true;
    }
  } else {
    pending_rendered_.insert(identity, result.image);
    if (result.image.isNull()) {
      ++state.mermaid_stats.failures;
    }
    const CachedImage cached{result.image.png_bytes, result.image.layout_width,
                             result.image.layout_height};
    state.mermaid_cache.Insert(result.task.cache_key, cached);
    if (!result.image.isNull()) {
      const QString disk_key =
          MakeRenderCacheKey(QStringLiteral("mermaid"), result.task.payload,
                             MermaidRenderVariant(result.task.theme));
      const RenderCacheConfig cache_config =
          MakeRenderCacheConfig(state.options, QStringLiteral("mermaid"));
      if (cache_config.enabled) {
        StoreRenderCacheEntry(cache_config, QStringLiteral("mermaid"), disk_key,
                              cached);
      }
    }
    if (result.loaded) {
      mermaid_engine_status_.loaded = true;
      if (mermaid_engine_status_.first_load_ms == 0) {
        mermaid_engine_status_.first_load_ms = result.first_load_ms;
      }
    }
    if (result.engine_all_failed) {
      mermaid_engine_status_.permanently_failed = true;
    }
  }

  if (!result.artifacts.isEmpty()) {
    const MarkdownDiagnostics diagnostics = context_->diagnostics();
    for (const MarkdownRenderArtifact& artifact : result.artifacts) {
      DumpRenderArtifact(diagnostics, artifact.name, artifact.bytes);
    }
  }

  ++pending_completed_;
  if (pending_in_flight_ > 0) {
    --pending_in_flight_;
  }
  emit renderProgress(request_id, pending_completed_, pending_total_,
                      pending_in_flight_);
}

void MarkdownView::OnAllFinished(const QString& view_id, quint64 request_id) {
  if (view_id != view_id_ || request_id != active_request_id_ || !rendering_) {
    if (view_id == view_id_) {
      ++render_history_.stale_results;
    }
    return;
  }
  CommitRender(request_id);
}

void MarkdownView::CommitRender(quint64 request_id) {
  if (request_id != active_request_id_ || !rendering_) {
    return;
  }
  QHash<QString, QString> code_blocks;
  QHash<QString, QString> heading_ids;
  QVector<MarkdownHeading> outline;
  QHash<QString, PinnedImage> pixmaps;
  QHash<QString, MarkdownImageInfo> image_info;
  MarkdownRenderOptions options;
  options.code_line_numbers = code_line_numbers_;
  options.outline = [&outline](const MarkdownHeading& heading) {
    outline.append(heading);
  };
  const MarkdownRenderResolver resolver =
      [this, &pixmaps, &image_info](MarkdownRenderKind kind,
                                    const QString& payload,
                                    const QString& language, bool display) {
        QString key;
        switch (kind) {
          case MarkdownRenderKind::Highlight:
            key = HighlightRenderKey(payload, language);
            break;
          case MarkdownRenderKind::Formula:
            key = FormulaRenderKey(payload, display, ThemeFormulaColor(theme_));
            break;
          case MarkdownRenderKind::Mermaid:
            key = MermaidRenderKey(payload, theme_);
            break;
        }
        const auto it = pending_rendered_.constFind(RenderIdentity(kind, key));
        if (it == pending_rendered_.constEnd()) {
          return QString();
        }
        if (kind == MarkdownRenderKind::Formula) {
          const QString token = MakeImageToken(QStringLiteral("f/"), key);
          const auto& image = std::get<RenderedImage>(it.value());
          if (image.isNull()) {
            return QString();  // 渲染失败，调用方按原文兜底
          }
          const QString tag = RegisterImage(
              token, image,
              display ? QStringLiteral(" class=\"math-display\"") : QString(),
              &pixmaps);
          if (!tag.isEmpty()) {
            MarkdownImageInfo info;
            info.kind = MarkdownImageKind::kFormula;
            info.source = payload.trimmed();
            info.display = display;
            image_info.insert(token, info);
          }
          return tag;
        }
        if (kind == MarkdownRenderKind::Mermaid) {
          const QString token = MakeImageToken(QStringLiteral("m/"), key);
          const auto& image = std::get<RenderedImage>(it.value());
          if (image.isNull()) {
            return QString();  // 渲染失败，调用方按原文兜底
          }
          const QString tag = RegisterImage(
              token, image, QStringLiteral(" class=\"mermaid\""), &pixmaps);
          if (!tag.isEmpty()) {
            MarkdownImageInfo info;
            info.kind = MarkdownImageKind::kMermaid;
            info.source = payload;
            image_info.insert(token, info);
          }
          return tag;
        }
        return std::get<QString>(it.value());
      };
  QElapsedTimer assemble_clock;
  assemble_clock.start();
  const QString body = AssembleMarkdown(
      pending_parse_, resolver,
      [&code_blocks](const QString& id, const QString& code) {
        code_blocks.insert(id, code);
      },
      [&heading_ids](const QString& slug, const QString& id) {
        heading_ids.insert(slug, id);
      },
      options);
  pending_timings_.assemble_ms = assemble_clock.elapsed();
  const QString final_html = WrapDocument(body, CssForTheme(theme_));

  document_stats_.source_chars = markdown_.size();
  document_stats_.html_chars = final_html.size();
  document_stats_.headings = outline.size();
  document_stats_.code_blocks = pending_parse_.blocks.size();
  document_stats_.formulas = pending_parse_.segments.size();
  document_stats_.mermaid_blocks = 0;
  for (const MarkdownCodeBlock& block : pending_parse_.blocks) {
    if (block.language == QLatin1String("mermaid")) {
      ++document_stats_.mermaid_blocks;
    }
  }

  const MarkdownDiagnostics diagnostics = context_->diagnostics();
  if (diagnostics.trace) {
    TraceRender(diagnostics,
                QStringLiteral("render %1: source=%2 chars, html=%3 chars")
                    .arg(request_id)
                    .arg(markdown_.size())
                    .arg(final_html.size()));
  }
  if (DumpEnabled(diagnostics, QStringLiteral("apply/source.md"))) {
    const int index = ++dump_apply_index_;
    DumpRenderArtifact(diagnostics,
                       QStringLiteral("apply/%1-source.md").arg(index),
                       markdown_.toUtf8());
    DumpRenderArtifact(diagnostics,
                       QStringLiteral("apply/%1-final.html").arg(index),
                       final_html.toUtf8());
  }

  // 先 setUrl 再 setHtml：setHtml 会立刻渲染，顺序反了首帧的相对图片与相对
  // 链接会按错误的基准解析
  html_->setUrl(BaseUrlForDir(base_dir_));
  code_blocks_ = std::move(code_blocks);
  heading_ids_ = std::move(heading_ids);
  outline_ = std::move(outline);
  document_pixmaps_ = std::move(pixmaps);
  image_info_ = std::move(image_info);
  committed_file_ = current_file_;
  committed_base_dir_ = base_dir_;
  committed_markdown_ = markdown_;
  committed_html_ = final_html;
  has_committed_ = !markdown_.isEmpty() || !current_file_.isEmpty();
  placeholder_visible_ = false;
  placeholder_scroll_ = 0;
  zoom_timer_->stop();
  html_->setZoomFactorWithoutRender(zoom_);
  QElapsedTimer layout_clock;
  layout_clock.start();
  html_->setHtml(final_html);
  pending_timings_.layout_ms = layout_clock.elapsed();
  if (pending_scroll_.has_value()) {
    html_->verticalScrollBar()->setValue(*pending_scroll_);
  }
  const int scroll = html_->verticalScrollBar()->value();
  pending_timings_.total_ms =
      request_clock_.isValid() ? request_clock_.elapsed() : 0;
  last_render_timings_ = pending_timings_;
  ++render_history_.requests_finished;
  InvalidateAnchorYCache();
  rendering_ = false;
  active_request_id_ = 0;
  pending_parse_ = MarkdownParseResult();
  pending_rendered_.clear();
  pending_total_ = 0;
  pending_completed_ = 0;
  pending_in_flight_ = 0;
  const bool notify_reload = pending_notify_reload_;
  const bool notify_viewport = pending_notify_viewport_;
  pending_notify_reload_ = false;
  pending_notify_viewport_ = false;
  pending_scroll_.reset();

  const QPointer<MarkdownView> guard(this);
  emit outlineChanged();
  if (guard.isNull()) {
    return;
  }
  emit renderStatusChanged();
  if (guard.isNull()) {
    return;
  }
  emit renderFinished(request_id);
  if (guard.isNull()) {
    return;
  }
  if (notify_reload) {
    emit documentReloaded();
    if (guard.isNull()) {
      return;
    }
  }
  if (notify_viewport) {
    emit viewportScrolled(scroll);
  }
}

void MarkdownView::CancelPendingRender() {
  if (active_request_id_ != 0) {
    ++render_history_.requests_cancelled;
    if (render_service_ != nullptr) {
      render_service_->Cancel(view_id_, active_request_id_);
    }
  }
  ++render_request_id_;
  active_request_id_ = 0;
  rendering_ = false;
  pending_notify_reload_ = false;
  pending_notify_viewport_ = false;
  pending_scroll_.reset();
  pending_parse_ = MarkdownParseResult();
  pending_rendered_.clear();
  pending_total_ = 0;
  pending_completed_ = 0;
  pending_in_flight_ = 0;
  // 回滚到屏幕上仍显示的已提交代，避免下一次 Reapply/热重载把取消的
  // 待提交文档又拉回来
  markdown_ = committed_markdown_;
  current_file_ = committed_file_;
  base_dir_ = committed_base_dir_;
  WatchCurrentFile();
  // 占位页盖住了已提交文档：取消时把屏幕也一起回滚
  if (placeholder_visible_) {
    placeholder_visible_ = false;
    zoom_timer_->stop();
    html_->setZoomFactorWithoutRender(zoom_);
    html_->setUrl(BaseUrlForDir(committed_base_dir_));
    html_->setHtml(committed_html_);
    html_->verticalScrollBar()->setValue(placeholder_scroll_);
    placeholder_scroll_ = 0;
  }
}

QString MarkdownView::RegisterImage(const QString& token,
                                    const RenderedImage& image,
                                    const QString& css_class,
                                    QHash<QString, PinnedImage>* pixmaps) {
  if (image.isNull()) {
    return QString();  // 渲染失败，调用方按原文兜底
  }
  MarkdownRenderContextState& state =
      MarkdownRenderContextAccess::State(*context_);
  if (const PixmapCacheEntry* cached = state.pixmap_cache.object(token)) {
    ++state.image_stats.image_cache_hits;
    // 跨代命中：缓存条目没有 png_bytes，pin 层置空，导出回退重编码
    pixmaps->insert(token, PinnedImage{cached->pixmap, QByteArray()});
    return cached->img_tag;
  }
  ++state.image_stats.image_cache_misses;

  // 调用方已判过 isNull：这里必然能出图，GUI 全程零解码零 base64
  const QPixmap pixmap = QPixmap::fromImage(image.image);
  const QString img_tag = QStringLiteral(
                              "<img%1 src=\"%2\" width=\"%3\" "
                              "height=\"%4\">")
                              .arg(css_class, token)
                              .arg(image.layout_width)
                              .arg(image.layout_height);
  constexpr qint64 kMaxCost = std::numeric_limits<int>::max();
  const qint64 cost =
      qint64(pixmap.width()) * pixmap.height() * 4 + qint64(img_tag.size()) * 2;
  auto* entry = new PixmapCacheEntry{pixmap, img_tag};
  // insert 失败时 QCache 已删除 entry，之后不能再碰它；pin 层仍保证当前文档可用
  state.pixmap_cache.insert(token, entry,
                            static_cast<int>(qMin(cost, kMaxCost)));
  pixmaps->insert(token, PinnedImage{pixmap, image.png_bytes});
  return img_tag;
}

void MarkdownView::ShowMessage(const QString& text) {
  // 错误页是新的已提交状态：旧文档的 copy/锚点/大纲不能继续指向看不见的页
  const bool had_outline = !outline_.isEmpty();
  committed_markdown_.clear();
  committed_file_.clear();
  committed_base_dir_.clear();
  committed_html_.clear();
  has_committed_ = false;
  placeholder_visible_ = false;
  placeholder_scroll_ = 0;
  code_blocks_.clear();
  heading_ids_.clear();
  outline_.clear();
  document_pixmaps_.clear();
  image_info_.clear();
  InvalidateAnchorYCache();
  // 错误页同样走 setHtml：先落地挂起缩放，避免先按旧缩放排一次
  zoom_timer_->stop();
  html_->setZoomFactorWithoutRender(zoom_);
  html_->setHtml(WrapDocument(
      QStringLiteral("<p class=\"error\">%1</p>").arg(text.toHtmlEscaped()),
      CssForTheme(theme_)));
  if (had_outline) {
    emit outlineChanged();
  }
  emit renderStatusChanged();
}

void MarkdownView::ClearDocument() {
  CancelPendingRender();
  markdown_.clear();
  base_dir_.clear();
  current_file_.clear();
  // 已提交的 code_blocks_/heading_ids_/outline_/pixmap 保持到下一代替换：
  // 旧 HTML 还在屏幕上，copy:/锚点/图片必须继续可用
  reload_attempts_ = 0;
  if (reload_timer_ != nullptr) {
    reload_timer_->stop();
  }
  if (watcher_ != nullptr && !watcher_->files().isEmpty()) {
    watcher_->removePaths(watcher_->files());
  }
}

void MarkdownView::WatchCurrentFile() {
  if (watcher_ == nullptr) {
    return;
  }
  if (!watcher_->files().isEmpty()) {
    watcher_->removePaths(watcher_->files());
  }
  if (!current_file_.isEmpty() && !watcher_->addPath(current_file_)) {
    // 监视不上（文件不存在、或系统 inotify/watch 名额满了）就等于没有热重载，
    // 说一声比静默失效强
    emit loadFailed(current_file_,
                    QStringLiteral("无法监视文件变化，热重载不可用"));
  }
}

void MarkdownView::OnFileChanged() {
  if (current_file_.isEmpty()) {
    return;
  }

  // 编辑器保存常是"先删后建"，watcher 会因此丢掉这个文件；等它落盘再挂回去。
  // 单次延时重试在慢盘、大文件、改名保存的编辑器上会扑空，而扑空之后就再没有
  // 任何机会挂回去（热重载从此静默失效），所以这里是有界轮询
  if (watcher_ != nullptr && !watcher_->files().contains(current_file_)) {
    if (!reload_timer_->isActive()) {
      reload_attempts_ = 0;
      reload_timer_->start();
    }
    return;
  }

  // 重载读不到内容时保留原样，免得存盘的一瞬间看到空白页。滚动位置也留着：
  // 一次保存就把人弹回页首，看长文档时很难受
  QString text;
  if (!ReadMarkdownFile(current_file_, &text)) {
    emit loadFailed(current_file_, QStringLiteral("重载失败，保留原内容"));
    return;
  }
  markdown_ = text;
  ApplyDocument(true, true);
}

void MarkdownView::OnReloadTick() {
  if (current_file_.isEmpty()) {
    reload_timer_->stop();
    return;
  }
  if (QFile::exists(current_file_)) {
    reload_timer_->stop();
    reload_notify_on_load_ = true;
    if (!LoadFile(current_file_)) {
      reload_notify_on_load_ = false;
    }
    return;
  }
  if (++reload_attempts_ >= kReloadMaxAttempts) {
    reload_timer_->stop();
    emit loadFailed(current_file_, QStringLiteral("文件已不在，停止监视"));
  }
}
