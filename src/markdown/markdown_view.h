#ifndef FILE_MARKDOWN_MARKDOWN_VIEW_H_
#define FILE_MARKDOWN_MARKDOWN_VIEW_H_

#include <array>
#include <memory>
#include <optional>
#include <variant>

#include <QColor>
#include <QElapsedTimer>
#include <QHash>
#include <QPixmap>
#include <QString>
#include <QUrl>
#include <QVector>
#include <QWidget>

#include "markdown_render_tasks.h"
#include "markdown_theme.h"
#include "markdown_to_html.h"
#include "markdown_types.h"

class MarkdownRenderContext;
class MarkdownRenderService;
struct MarkdownRenderResult;
class QFileSystemWatcher;
class QLiteHtmlWidget;
class QPrinter;

// copy: 链接（代码块"复制"按钮）→ 要复制的原文；不是 copy: 或表里查不到
// 返回空串。抽成自由函数是为了可测：代码块由 qlitehtml 自绘，合成鼠标
// 事件到不了链接命中判定；"信号之后"的拦截与查表才是本模块的逻辑。
// 空串 = 别动剪贴板（宁可没反应，也不能把用户剪贴板里原来的东西清掉）
QString CopyTextForLink(const QUrl& url, const QHash<QString, QString>& blocks);

// 资源回调：图片/公式图的 URL → 字节。两道闸：
//  - data: URI 只认 PNG，解码量上限 16MB（恶意 md 塞 GB 级 base64 会在
//    解码那一刻吃满内存）；
//  - 本地文件必须落在 base_dir 之内（文档里写 <img src="/etc/passwd"> 或
//    file:///…/.ssh/id_rsa 不该被读出来）。比对 canonical 路径，符号链接
//    按解析后的位置算；base 为空时本地文件一律拒绝
QByteArray ReadResourceForUrl(const QUrl& url, const QString& base_dir);

// 由渲染器缓存 key 生成稳定的图片 token（mj:f/<sha256> / mj:m/<sha256>）。
// 抽成自由函数是为了可测：token 形态与 qlitehtml 解析出的 QUrl 必须对得上
QString MakeImageToken(const QString& prefix, const QString& cache_key);

enum class MarkdownImageKind {
  kFormula,
  kMermaid,
};

// 公式/Mermaid 图片对应的源码。普通图片没有这个信息
struct MarkdownImageInfo {
  MarkdownImageKind kind = MarkdownImageKind::kFormula;
  QString source;
  bool display = false;
};

// 当前文档 pin 住的图片：位图供 provider 绘制；png_bytes 非空时导出直写
// （worker 编的那次 PNG 就是落盘那次），为空（跨代缓存命中路径）则导出
// 回退 QPixmap 重编码
struct PinnedImage {
  QPixmap pixmap;
  QByteArray png_bytes;
};

// Markdown 预览控件：加载 .md、套内置样式渲染、跟随文件变化重载。
// 只做"渲染"这一件事——链接点击原样抛出（copy: 的代码块复制链接除外，那
// 是控件内部能力：查表放剪贴板，见 ApplyDocument）；本地 .md 是自开还是
// 交给系统浏览器是宿主的策略；qlitehtml 的类型不出现在本头文件里，换渲染
// 后端不动调用方。
//
// 内置交互：Ctrl+滚轮 / Ctrl+= / Ctrl+- 缩放（0.3~5.0）、Ctrl+0 复位、
// Ctrl+C 复制选中文本（QAbstractScrollArea 的默认按键处理会吞掉 Copy，
// 必须拦）。
class MarkdownView : public QWidget {
  Q_OBJECT

 public:
  // 查找选项。自定义而不用 QTextDocument::FindFlags——宿主不该为了几个
  // 枚举值把 QTextDocument 拉进编译单元
  enum FindFlag {
    FindNoFlag = 0,
    FindBackward = 1,
    FindCaseSensitively = 2,
    FindWholeWords = 4,
  };
  Q_DECLARE_FLAGS(FindFlags, FindFlag)
  explicit MarkdownView(QWidget* parent = nullptr);
  ~MarkdownView() override;

  // 从磁盘加载，成功返回 true。基准 URL 取文件所在目录，相对路径的图片与
  // 链接据此解析；失败时视图内提示原因，并回到"没有文档"的状态
  bool LoadFile(const QString& path);
  // 直接给文本。基准 URL 就是 base_dir（空 = 没有基准），不给上一份文档的目录
  // 留后门——否则相对图片会悄悄按上次那个目录解析
  void SetContent(const QString& markdown, const QString& base_dir);
  // 立即换成一段占位页，不改已提交文档的状态；下一次提交整体覆盖。
  // 换文档/换主题耗时时先给用户明确反馈，避免旧页面看起来像没响应
  void ShowPlaceholder(const QString& text);
  // 最后已提交的文件；派发中的新文件在 renderFinished 后才可见
  QString current_file() const { return committed_file_; }
  // 两种载入都算"有文档"：LoadFile 落 current_file_，SetContent 只落
  // markdown_——只查后者的话文本直载后这里恒为 false
  // 只反映最后已提交代；渲染中的新文档要等 renderFinished
  bool has_document() const { return has_committed_; }
  // 是否有一代正在解析/渲染；宿主据此避免在旧文档上做即时滚动
  bool is_rendering() const { return rendering_; }
  // 正在渲染的目标文件（SetContent 无文件时为空）
  QString pending_file() const {
    return rendering_ ? current_file_ : QString();
  }
  // 取消当前派发；旧结果会被 request_id 过滤，worker 本身仍会跑完
  void CancelPendingRender();

  // 滚到页内锚点：用户写的是 slug（`#标题`），这里经内部的 slug→id 表换成
  // 元素再滚；查不到就原样透传（手写的 <a name="x"> 那类归 litehtml 自己找）
  void ScrollToAnchor(const QString& anchor);

  // 查找并选中命中处（找到会滚过去）。wrapped 非空时被写成"本次是否绕回了
  // 开头"（每次调用都会写——控件本体只在绕回时写，见实现），宿主拿它提示
  // "已到末尾"。返回 false = 没有命中，选区不动。
  // incremental=true 从当前选区开头找，供"边打字边找"使用；默认 false 从
  // 选区末尾找下一个，适合 F3/回车
  bool FindText(const QString& text, FindFlags flags, bool* wrapped = nullptr,
                bool incremental = false);
  // 清掉查找/鼠标选区；搜索框清空时由宿主调用
  void ClearSelection();
  bool has_selection() const;
  // 导出 HTML + 同名资源目录。图片复制/嵌入规则见实现与设计文档；
  // 渲染中或没有已提交文档时返回 false
  bool ExportHtml(const QString& html_path, QString* error = nullptr) const;
  // 图片 URL 的像素。支持 mj: token、data: PNG 和文档目录内的本地图片；
  // 远程图片返回空 QPixmap
  QPixmap ImagePixmap(const QUrl& url) const;
  // mj: 图片对应的源码；普通图片返回 nullopt
  std::optional<MarkdownImageInfo> ImageInfo(const QUrl& url) const;
  // 打印/导出 PDF：QPrinter 由宿主自备（本控件不碰文件系统与输出格式）。
  // 控件按 print 媒体重排一遍再逐页绘制——分页是按页高切片的，跨页的长
  // 元素会被切在页缝上
  void Print(QPrinter* printer);

  // 代码块行号（默认关）；切换后若有文档立即重渲染并保留滚动位置
  void SetCodeLineNumbers(bool enabled);
  bool code_line_numbers() const { return code_line_numbers_; }
  // 公式/Mermaid 渲染（默认开）。关闭时不传对应 sink：$…$ 原样显示、
  // mermaid 围栏退回代码块，且不会触发 JS 载荷加载
  void SetFormulaRenderingEnabled(bool enabled);
  bool formula_rendering_enabled() const { return formula_rendering_enabled_; }
  void SetMermaidRenderingEnabled(bool enabled);
  bool mermaid_rendering_enabled() const { return mermaid_rendering_enabled_; }
  // 主题（默认 Light）：样式表、公式前景色、Mermaid 主题一起切。切换后
  // 有文档立即重渲染并保留滚动；缓存按 key 自然隔离，不清空。
  // MD_MARKDOWN_CSS 生效时 CSS 保持覆盖文件（theme 对样式表无效），只切
  // 公式/Mermaid 配色，可能配色错位——切换时打一次 qWarning
  void SetTheme(MarkdownTheme theme);
  MarkdownTheme theme() const { return theme_; }
  // 宿主样式：按主题注入完整 CSS；空串清除，回退到 MD_MARKDOWN_CSS/内置
  // 主题。显式设置优先于 MD_MARKDOWN_CSS，且不会触发主题错配 warning
  void SetThemeStyleSheet(MarkdownTheme theme, const QString& css);
  // 追加样式：拼在主题基准 CSS（显式样式 / MD_MARKDOWN_CSS / 内置主题）
  // 之后，用于字号、行距这类局部覆盖；空串清除追加层，基准样式不受影响
  void AppendThemeStyleSheet(MarkdownTheme theme, const QString& css);
  // 页面背景：Qt::transparent 表示透明，其它颜色填充 html viewport。
  // 默认不调用时保持原有行为；透明还需要 CSS 把 html/body 写成 transparent
  void SetPageBackground(const QColor& color);
  // 缩放（0.3~5.0）。连续调用只在停手 150ms 后应用一次，避免每档都整篇重排；
  // 换文档/查找/锚点跳转会先把挂起的缩放落地
  void SetZoomFactor(qreal factor);
  qreal zoom() const { return zoom_; }
  // 按内置步进缩放；按钮与快捷键共用同一档位
  void ZoomIn();
  void ZoomOut();
  void ResetZoom();

  // 垂直滚动位置（litehtml 文档坐标，单位与 viewportScrolled 一致）。
  // 历史恢复用：宿主只记值，不碰内部滚动条
  int ScrollPosition() const;
  void SetScrollPosition(int y);
  // 整篇 litehtml 布局的累计次数：纯高度 resize 不增长，宽度/缩放/换文档
  // 才增长。给示例与排查用，不参与渲染逻辑
  int layout_count() const;
  // 本视图的渲染状态（不触发加载）与计数清零
  MarkdownRendererStatus renderer_status() const;
  void ResetRendererStats();
  // 共享池实时状态（worker/队列/处理中/JS 堆）；未派发过渲染时为零
  MarkdownServiceStatus render_service_status() const;
  // 最近一次已提交文档的统计、阶段耗时与整个视图的请求历史
  MarkdownDocumentStats document_stats() const { return document_stats_; }
  MarkdownRenderTimings last_render_timings() const {
    return last_render_timings_;
  }
  MarkdownRenderHistory render_history() const { return render_history_; }
  // 诊断：trace 打 qt.markdown.render；dump_directory 落中间产物。
  // 只影响观测，不改变 HTML，因此不触发重渲染
  void SetDiagnostics(const MarkdownDiagnostics& diagnostics);
  MarkdownDiagnostics diagnostics() const;

  // 本次文档的标题大纲（按出现顺序，anchor 可直接传 ScrollToAnchor）；
  // 每次 ApplyDocument 重建并发出 outlineChanged
  const QVector<MarkdownHeading>& outline() const { return outline_; }
  // 标题的文档 y 坐标（单位与垂直滚动条一致）。0 合法（标题在文档最顶部），
  // 找不到锚点返回 nullopt
  std::optional<int> AnchorY(const QString& anchor) const;

  // 本视图的渲染上下文（缓存/计数/配置）。需要改缓存目录、容量或读
  // renderer_status() 时用；生命周期跟随 MarkdownView
  MarkdownRenderContext& render_context();
  const MarkdownRenderContext& render_context() const;
  // 清本视图的公式/Mermaid 内存缓存与跨 ApplyDocument 图片缓存；
  // 当前文档已生成的 token 仍保留，PDF 导出不会缺图
  void ClearRenderCaches();
  // 持久化渲染缓存（默认开）：公式/Mermaid 的最终 <img> 落在
  // QStandardPaths::CacheLocation/markdown-render；SetRenderCacheDirectory
  // 可换成指定根目录。关闭只跳过磁盘读写，内存缓存照常
  void SetPersistentRenderCacheEnabled(bool enabled);
  bool persistent_render_cache_enabled() const;
  void SetRenderCacheDirectory(const QString& dir);
  // 只删本模块的 formula/ 与 mermaid/ 子目录，不动根目录下的其他内容
  void ClearPersistentRenderCache();

 signals:
  void linkClicked(const QUrl& url);
  // 左键点击图片（press/release 同图、非链接）与图片右键菜单请求。
  // 宿主用 ImageInfo()/ImagePixmap() 决定预览、另存为或查看源码
  void imageClicked(const QUrl& url);
  void imageContextMenuRequested(const QPoint& global_pos, const QUrl& url);
  void loadFailed(const QString& path, const QString& reason);
  // 文档重建后大纲变化（含清空文档）
  void outlineChanged();
  // 垂直滚动位置变化，宿主据此反查当前章节
  void viewportScrolled(int y);
  // 缩放请求值变化；实际重排在 150ms 内合并
  void zoomChanged(qreal factor);
  // 文档渲染完成，renderer_status() 里的引擎状态与计数已更新
  void renderStatusChanged();
  // 异步渲染进度：completed/total 可用于百分比；total == 0 表示无任务
  void renderProgress(quint64 request_id, int completed, int total,
                      int in_flight);
  // 一次派发完成并已提交；宿主按 request_id 过滤旧代
  void renderFinished(quint64 request_id);
  // 文件内容变化后成功重载（watchdog 路径）
  void documentReloaded();
  // 本地资源（图片）读取失败；data: URI 与 mj: token 不计入
  void resourceLoadFailed(const QUrl& url);
  // 代码块的“复制”链接已把源码写进剪贴板
  void codeCopied();

 protected:
  bool eventFilter(QObject* watched, QEvent* event) override;

 private:
  void initUi();
  void initSignals();
  void ApplyDocument(bool preserve_scroll = false,
                     bool notify_document_reloaded = false);
  // 解析、收集、缓存命中与派发；在事件循环里执行
  void StartRender(quint64 request_id);
  void OnTaskFinished(quint64 request_id, const MarkdownRenderResult& result);
  void OnAllFinished(const QString& view_id, quint64 request_id);
  void CommitRender(quint64 request_id);
  // 重渲染并保留垂直滚动位置（开关切换用）
  void ReapplyDocument();
  QString CssForTheme(MarkdownTheme theme) const;
  void ApplyPageBackground();
  void ShowMessage(const QString& text);
  void ClearDocument();
  void WatchCurrentFile();
  void OnFileChanged();
  void OnReloadTick();
  void SetZoom(qreal factor);
  // 由 worker 产物生成 token 图并拼 <img>：命中跨文档缓存就直接复用，
  // 否则 fromImage 一次。image 的位图/PNG 都来自本代渲染或缓存解码，
  // GUI 线程不再有 base64/PNG 解码以外的字符串手术。当前文档的 token
  // 一律 pin 进 document_pixmaps_，provider 只读这一层
  QString RegisterImage(const QString& token, const RenderedImage& image,
                        const QString& css_class,
                        QHash<QString, PinnedImage>* pixmaps);
  // 定时器到点：应用最后一档缩放
  void ApplyPendingZoom();
  // 立刻应用挂起的缩放（查找/锚点跳转前调用）
  void FlushPendingZoom();
  // 锚点 y 的缓存失效：换文档时显式调用；布局变化靠 layoutCount 自动发现
  void InvalidateAnchorYCache();

  // 每视图一份渲染状态（缓存/计数/配置）；引擎由同步入口的线程局部
  // 默认实例提供，异步 worker 各自持有
  std::unique_ptr<MarkdownRenderContext> context_;
  // 进程级共享服务；首次派发时才取实例
  MarkdownRenderService* render_service_ = nullptr;
  QString view_id_;
  QLiteHtmlWidget* html_ = nullptr;
  QFileSystemWatcher* watcher_ = nullptr;
  // "先删后建"保存时用来等文件回来，见 OnFileChanged
  QTimer* reload_timer_ = nullptr;
  // 缩放合并：连续滚轮/快捷键只应用最后一档
  QTimer* zoom_timer_ = nullptr;
  int reload_attempts_ = 0;
  QString markdown_;
  QString current_file_;
  QString committed_file_;
  QString base_dir_;
  // 资源回调按最后已提交文档的目录校验；派发期间不能提前换掉
  QString committed_base_dir_;
  QString committed_markdown_;
  QString committed_html_;
  bool has_committed_ = false;
  // 占位页只是屏幕上的临时内容；取消派发时用它回滚到已提交页
  bool placeholder_visible_ = false;
  int placeholder_scroll_ = 0;
  // 代码块 id → 原始代码文本，供"复制"链接取用；每次 ApplyDocument 重建
  QHash<QString, QString> code_blocks_;
  // 标题 slug → 元素 id，供页内锚点反查；同样每次 ApplyDocument 重建
  QHash<QString, QString> heading_ids_;
  // 当前文档的图片 token → pin 图；ApplyDocument 开头清空，provider 只读它
  QHash<QString, PinnedImage> document_pixmaps_;
  // 当前文档的 mj: token → 公式/Mermaid 源码
  QHash<QString, MarkdownImageInfo> image_info_;
  // 本次文档的标题大纲；与 heading_ids_ 同源
  QVector<MarkdownHeading> outline_;
  // 锚点 → 文档 y。litehtml 的 anchorY 是 DOM 树搜索，滚动处理器按大纲
  // 反查当前章节时不能每帧每个标题各查一次；首次查询一次性填满
  mutable QHash<QString, int> anchor_y_cache_;
  mutable bool anchor_y_cache_dirty_ = true;
  // 缓存对应的布局代数；纯高度 resize 不会改变它，缓存得以保留
  mutable int anchor_y_layout_count_ = -1;
  qreal zoom_ = 1.0;
  bool code_line_numbers_ = false;
  bool formula_rendering_enabled_ = true;
  bool mermaid_rendering_enabled_ = true;
  MarkdownTheme theme_ = MarkdownTheme::Light;
  // 宿主按主题注入的 CSS；空 = 走 MD_MARKDOWN_CSS/内置资源
  std::array<QString, kMarkdownThemeCount> theme_style_sheets_{};
  // 追加层：只在渲染时拼接，不改变 SetThemeStyleSheet 的“整份替换”语义
  std::array<QString, kMarkdownThemeCount> theme_style_sheet_appends_{};
  QColor page_background_ = Qt::transparent;
  bool page_background_set_ = false;
  // 转储时给每次 ApplyDocument 的源/最终 HTML 编号（仅诊断用）
  int dump_apply_index_ = 0;

  // 异步渲染状态。pending_* 只在 GUI 线程读写，提交前不碰已提交表
  quint64 render_request_id_ = 0;
  quint64 active_request_id_ = 0;
  bool rendering_ = false;
  bool pending_notify_reload_ = false;
  bool pending_notify_viewport_ = false;
  bool reload_notify_on_load_ = false;
  std::optional<int> pending_scroll_;
  MarkdownParseResult pending_parse_;
  // identity -> 高亮 HTML 或渲染图（判别以 task.kind 为准，variant 只做
  // "存对了没有"的断言）
  QHash<QString, std::variant<QString, RenderedImage>> pending_rendered_;
  int pending_total_ = 0;
  int pending_completed_ = 0;
  int pending_in_flight_ = 0;
  MarkdownEngineStatus highlight_engine_status_;
  MarkdownEngineStatus formula_engine_status_;
  MarkdownEngineStatus mermaid_engine_status_;
  QElapsedTimer request_clock_;
  MarkdownRenderTimings pending_timings_;
  MarkdownRenderTimings last_render_timings_;
  MarkdownDocumentStats document_stats_;
  MarkdownRenderHistory render_history_;
};

Q_DECLARE_OPERATORS_FOR_FLAGS(MarkdownView::FindFlags)

#endif  // FILE_MARKDOWN_MARKDOWN_VIEW_H_
