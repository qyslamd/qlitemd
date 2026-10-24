#ifndef FILE_EXAMPLES_MD_VIEWER_MD_VIEWER_WINDOW_H_
#define FILE_EXAMPLES_MD_VIEWER_MD_VIEWER_WINDOW_H_

#include <optional>

#include <QByteArray>
#include <QColor>
#include <QMainWindow>
#include <QPoint>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVector>

#include "bookmarks.h"
#include "markdown_types.h"
#include "md_viewer_settings.h"
#include "navigation_history.h"

class QAction;
class QActionGroup;
class QCloseEvent;
class QDockWidget;
class QDragEnterEvent;
class QDragMoveEvent;
class QDropEvent;
class QFileSystemWatcher;
class QLabel;
class QLineEdit;
class QMenu;
class QProgressBar;
class QResizeEvent;
class QTimer;
class QToolButton;
class QVariantAnimation;
class DocumentsPanel;
class ReadingProgressBar;
class WelcomeView;
class FluentWindowFrame;
class MarkdownOutlineWidget;
class MarkdownView;
class QuickOpenPopup;
class ReaderAppearancePopup;
class RenderSettingsPanel;
class RenderStatusPanel;
class TextSearch;
struct DocumentScanResult;
struct QuickOpenItem;
struct ReaderPreferences;
struct SearchHit;

// 独立的 Markdown 查看器：单篇与"整个目录"两种打开方式，目录铺在左侧递归
// 树里。渲染全交给 src/markdown 的 MarkdownView——本类只管宿主策略：
// 窗口壳、菜单、侧栏、状态栏。
class MdViewerWindow : public QMainWindow {
  Q_OBJECT

 public:
  explicit MdViewerWindow(MdViewerSettings* settings,
                          QWidget* parent = nullptr);
  ~MdViewerWindow() override;

  // 命令行参数进来：目录就铺开侧栏并看第一篇，文件直接看
  void OpenPath(const QString& path);
  // 启动时恢复上次根目录/文档；没有会话时退回最近文件
  void RestoreSession();

 protected:
  void closeEvent(QCloseEvent* event) override;
  void dragEnterEvent(QDragEnterEvent* event) override;
  void dragMoveEvent(QDragMoveEvent* event) override;
  void dropEvent(QDropEvent* event) override;
  void resizeEvent(QResizeEvent* event) override;
  bool eventFilter(QObject* watched, QEvent* event) override;

 private:
  void createActions();
  void initUi();
  void initSignals();

  // 统一的导航入口：同文件只滚锚点，不重加载；跨文件才走加载
  void NavigateTo(const QString& path, const QString& fragment,
                  bool record_history);
  // 真正打开路径（文件或目录），返回实际打开的文件；失败返回空
  QString OpenPathCore(const QString& path);
  enum class OpenReason { kUser, kRestore };
  bool ShowFile(const QString& path, OpenReason reason);
  void PushHistory(const QString& path, const QString& fragment);
  void ScrollToFragment(const QString& fragment);
  void GoBack();
  void GoForward();
  void UpdateHistoryActions();

  void OpenFileDialog();
  void OpenDirectoryDialog();
  // Ctrl+P：把当前标题/文件/最近/命令汇总到快速跳转面板
  void ShowQuickOpen();
  void ActivateQuickOpenItem(const QuickOpenItem& item);
  // Fluent 壳与文档主题一起切
  void ApplyTheme(MarkdownTheme theme);
  // 标题栏主题按钮：浅色/暗色之间翻转
  void ToggleTheme();
  // Windows：窗口材质（Mica/亚克力/关闭），调色板透明度与 backdrop 一起切
  void ApplyWindowMaterial(WindowMaterial material, bool announce);
  // 窄宽度下按顺序隐藏缩放组、搜索框、文档名
  void UpdateChromeVisibility();
  void FocusGlobalSearch();
  void StartSearch();
  void OpenSearchHit(const SearchHit& hit);
  void JumpToSearchHit(int ordinal, const QString& query);
  void ExportHtml();
  void ExportPdf();
  // 图片左键：公式/Mermaid 看源码，普通图片预览
  void OpenImage(const QUrl& url);
  void ShowImageContextMenu(const QPoint& global_pos, const QUrl& url);
  void SaveImageAs(const QUrl& url);
  void ShowImagePreview(const QUrl& url);
  void ShowImageSource(const QUrl& url);
  // 扫描并把根目录交给侧栏；返回本次扫描结果供调用方决定是否打开首篇
  DocumentScanResult SetRootDir(const QString& root);
  // 文件在当前根下就保持根；否则以其父目录为新根
  void EnsureRootForFile(const QString& path);
  void SetBookmarkChecked(bool starred);
  // 文档树行内星标走这个入口，不要求该文件就是当前显示的文件
  void ToggleBookmarkForPath(const QString& path);
  void UpdateBookmarkUi();
  void OpenBookmark(const QString& path);
  void SetFocusMode(bool enabled);
  void UpdateMaterialActions();
  // FluentUI 局部 palette 不会随 qApp->setPalette 自动刷新，切主题后
  // 显式把新调色板压给外壳控件和正文页
  void RefreshChromePalette();
  // 阅读区样式 opt-in：原型 CSS 或库默认；同时刷新页面背景
  void ApplyReadingStyle();
  void UpdateReadingSurface();
  // 阅读外观（字号/行距/栏宽）：偏好 CSS 以追加层注入，不改基准主题
  ReaderPreferences CurrentReaderPreferences() const;
  void ApplyReaderAppearance();
  void ShowReaderAppearance();
  // 无文档时的欢迎页，与正文互斥显示
  void ShowWelcome();
  void HideWelcome();
  // restoreState 之后采样欢迎页之前的大纲可见性与 dock 布局
  void CaptureWelcomeState();
  // 顶部进度线、浮动回顶按钮与状态栏阅读时长
  void UpdateReadingProgress(int y);
  // 等 qlitehtml 完成重排后再按新高度重算进度
  void ScheduleReadingProgressRefresh();
  void UpdateReadingTime();
  void PositionBackToTop();
  void ScrollToTop();
  // 当前主题的页面底色（与 resources/themes/*.css 的 body 对齐）
  QColor ReadingPageColor() const;
  void RememberCurrentPosition();
  void RefreshRoot();
  void OpenLastFile();
  void RefreshRecentMenu();
  // 关闭/退出时把窗口与渲染状态写回 md_viewer.ini
  void SaveSettings();
  void UpdateZoomLabel(qreal factor);
  // 文档渲染后对比上一次状态，把引擎加载与本次失败报到状态栏
  void ReportRenderStatus();

  // 菜单里挂着；菜单栏在 fluent 壳下会被搬进标题栏
  QAction* open_file_action_ = nullptr;
  QAction* open_dir_action_ = nullptr;
  QAction* quit_action_ = nullptr;
  QAction* about_action_ = nullptr;
  QAction* export_html_action_ = nullptr;
  QAction* export_pdf_action_ = nullptr;
  QAction* find_action_ = nullptr;
  QAction* quick_open_action_ = nullptr;
  QAction* back_action_ = nullptr;
  QAction* forward_action_ = nullptr;
  QAction* zoom_in_action_ = nullptr;
  QAction* zoom_out_action_ = nullptr;
  QAction* zoom_reset_action_ = nullptr;
  QAction* bookmark_action_ = nullptr;
  QAction* refresh_action_ = nullptr;
  QAction* focus_mode_action_ = nullptr;
  QAction* material_auto_action_ = nullptr;
  QAction* material_acrylic_action_ = nullptr;
  QAction* material_none_action_ = nullptr;
  // Fluent 标题栏自带的搜索框；控件归标题栏所有
  QLineEdit* search_edit_ = nullptr;
  // 输入防抖：连续打字只搜最后一档
  QTimer* search_timer_ = nullptr;
  TextSearch* search_ = nullptr;
  // 当前展示结果对应的查询；防抖窗口内旧扫描的排队信号据此丢弃
  std::optional<QString> active_search_query_;
  // 同一查询重开扫描时，查询文本兜不住旧一轮的排队信号，再加请求 id
  std::optional<quint64> active_search_id_;

  MarkdownView* view_ = nullptr;
  MarkdownOutlineWidget* outline_ = nullptr;
  RenderSettingsPanel* render_settings_ = nullptr;
  RenderStatusPanel* render_status_ = nullptr;
  DocumentsPanel* documents_panel_ = nullptr;
  QuickOpenPopup* quick_open_ = nullptr;
  ReaderAppearancePopup* appearance_popup_ = nullptr;
  WelcomeView* welcome_ = nullptr;
  ReadingProgressBar* reading_progress_ = nullptr;
  QWidget* reader_container_ = nullptr;
  QToolButton* back_to_top_ = nullptr;
  QVariantAnimation* scroll_animation_ = nullptr;
  // SetRootDir 时缓存的 Markdown 文件列表；快速跳转不重复扫盘
  QStringList quick_open_files_;
  QLabel* path_label_ = nullptr;
  QLabel* breadcrumb_ = nullptr;
  QLabel* zoom_label_ = nullptr;
  QLabel* status_ready_label_ = nullptr;
  QLabel* document_count_label_ = nullptr;
  QLabel* reading_time_label_ = nullptr;
  QLabel* backend_label_ = nullptr;
  QProgressBar* render_progress_ = nullptr;
  quint64 progress_request_ = 0;
  // 上一次渲染状态：只用来看引擎首次加载与本次失败增量
  MarkdownRendererStatus last_render_status_;
  // 当前文档集的根目录；单文件在根外时才换根
  QString root_dir_;
  // ShowFile 已经加载但可能还在渲染的文件；星标要认它，而不是
  // MarkdownView::current_file()（那要等 renderFinished 才提交）
  QString displayed_file_;
  BookmarkStore bookmarks_;
  ReadingPositionStore positions_;
  QSet<QString> large_file_prompted_;
  QFileSystemWatcher* root_watcher_ = nullptr;
  QTimer* root_rescan_timer_ = nullptr;
  QStringList watched_dirs_;
  int pending_restore_scroll_ = -1;
  QString pending_restore_path_;
  // 文件跳转与锚点跳转共用的历史栈
  NavigationHistory history_;
  // 换文档后要等的锚点；renderFinished 时再滚
  QString pending_fragment_;
  struct PendingJump {
    QString path;
    int ordinal = 0;
    QString query;
  };
  std::optional<PendingJump> pending_search_hit_;

  // 无边框窗口壳（QWK + Fluent 标题栏）
  FluentWindowFrame* fluent_frame_ = nullptr;
  QDockWidget* document_dock_ = nullptr;
  QDockWidget* outline_dock_ = nullptr;
  QDockWidget* settings_dock_ = nullptr;
  bool focus_mode_ = false;
  bool focus_restore_document_ = true;
  bool focus_restore_outline_ = true;
  // 欢迎页会临时收起空大纲；采样一次，避免重复 ShowWelcome 覆盖
  bool welcome_outline_was_visible_ = false;
  // 欢迎页显示期间关闭窗口时保存这份布局，别把临时隐藏写进配置
  QByteArray welcome_dock_state_;
  // 窄宽度自动收起大纲；用户手动关掉的不算，恢复宽度时也不自动弹开
  bool outline_auto_hidden_ = false;
  QWidget* nav_widget_ = nullptr;
  QToolButton* menu_button_ = nullptr;
  QToolButton* appearance_button_ = nullptr;
  QToolButton* zoom_out_button_ = nullptr;
  QToolButton* zoom_reset_button_ = nullptr;
  QToolButton* zoom_in_button_ = nullptr;
  QToolButton* star_button_ = nullptr;
  QMenu* app_menu_ = nullptr;
  QMenu* view_menu_ = nullptr;
  WindowMaterial window_material_ = WindowMaterial::kAuto;

  // 标准配置目录下的 md_viewer.ini，归 main 持有，生命周期长于本窗口
  MdViewerSettings* settings_ = nullptr;
  QMenu* recent_menu_ = nullptr;
};

#endif  // FILE_EXAMPLES_MD_VIEWER_MD_VIEWER_WINDOW_H_
