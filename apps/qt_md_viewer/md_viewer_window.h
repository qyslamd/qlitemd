#ifndef FILE_APPS_QT_MD_VIEWER_MD_VIEWER_WINDOW_H_
#define FILE_APPS_QT_MD_VIEWER_MD_VIEWER_WINDOW_H_

#include <array>
#include <memory>
#include <optional>
#include <vector>

#include <QByteArray>
#include <QColor>
#include <QMainWindow>
#include <QPoint>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVariant>
#include <QVector>

#include "document_tab.h"
#include "markdown_types.h"
#include "md_viewer_settings.h"
#include "navigation_history.h"
#include "side_view.h"

class QAction;
class QActionGroup;
class QCloseEvent;
class QDragEnterEvent;
class QDragMoveEvent;
class QDropEvent;
class QFileSystemWatcher;
class QLabel;
class QLineEdit;
class QMenu;
class QSplitter;
class QProgressBar;
class QResizeEvent;
class QStackedWidget;
class QTimer;
class QToolButton;
class ActivityBar;
class BookmarkController;
class ReadingPositionController;
class SessionController;
class TopToolBar;
class WelcomeBar;
class QVariantAnimation;
class SidePanel;
class DocumentBar;
class DocumentTabBar;
class ReaderPage;
class SettingsDialog;
class WelcomeView;
class FluentWindowFrame;
class MarkdownOutlineWidget;
class MarkdownView;
class QuickOpenPopup;
class ReaderAppearancePopup;
class SettingsPanel;
class RenderStatusPanel;
class SearchController;
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
  // session_owner：只有第一个窗口读写会话（open_tabs/几何/侧栏状态）
  explicit MdViewerWindow(MdViewerSettings* settings, bool session_owner = true,
                          QWidget* parent = nullptr);
  ~MdViewerWindow() override;

  // 命令行参数进来：目录就铺开侧栏并看第一篇，文件直接看
  void OpenPath(const QString& path);
  // 启动时恢复上次根目录/文档；没有会话时退回最近文件
  void RestoreSession();

  // 测试钩子：注入式 GUI 探针（funq）只能给单参 QVariant，这两个入口
  // 只做转发，不改变正常路径
  Q_INVOKABLE QVariant OpenPathForTest(const QVariant& path);
  Q_INVOKABLE QVariant OpenPathReplaceForTest(const QVariant& path);
  Q_INVOKABLE QVariant RootDirForTest(const QVariant& unused) const;
  Q_INVOKABLE QVariant TabSettingsForTest(const QVariant& index) const;
  Q_INVOKABLE QVariant ActivateTabForTest(const QVariant& index);
  Q_INVOKABLE QVariant TabStateForTest(const QVariant& unused) const;
  // 参数为 [from, to]，走标签条真实的 moveTab → tabMoved 链路
  Q_INVOKABLE QVariant MoveTabForTest(const QVariant& args);

 protected:
  void closeEvent(QCloseEvent* event) override;
  void dragEnterEvent(QDragEnterEvent* event) override;
  void dragMoveEvent(QDragMoveEvent* event) override;
  void dropEvent(QDropEvent* event) override;
  void resizeEvent(QResizeEvent* event) override;
  bool eventFilter(QObject* watched, QEvent* event) override;
  QMenu* createPopupMenu() override;

 private:
  void createActions();
  void initUi();
  void initSignals();

  enum class OpenReason { kUser, kRestore };
  // kReplace 替换当前标签；kNewTab 新开；kHistory 只给前进/后退用
  enum class OpenIntent { kReplace, kNewTab, kHistory };
  // 统一的导航入口：同文件只滚锚点，不重加载；跨文件才走加载
  void NavigateTo(const QString& path, const QString& fragment,
                  bool record_history,
                  OpenIntent intent = OpenIntent::kReplace);
  // 真正打开路径（文件或目录），返回实际打开的文件；失败返回空
  QString OpenPathCore(const QString& path, OpenIntent intent);
  bool ShowFile(const QString& path, OpenReason reason, OpenIntent intent);
  void PushHistory(const QString& path, const QString& fragment);
  void ScrollToFragment(const QString& fragment);
  void GoBack();
  void GoForward();
  void UpdateHistoryActions();

  void OpenFileDialog();
  void OpenDirectoryDialog();
  // 卸载工作区：清会话（root/last_file）回欢迎页，重启不再恢复；
  // 最近打开/书签/阅读位置保留
  void UnloadWorkspace();
  // Ctrl+P：把当前标题/文件/最近/命令汇总到快速跳转面板
  void ShowQuickOpen();
  void ActivateQuickOpenItem(const QuickOpenItem& item);
  // Fluent 壳与文档主题一起切
  void ApplyTheme(MarkdownTheme theme);
  // 标题栏主题按钮：浅色/暗色之间翻转
  void ToggleTheme();
  // Windows：窗口材质（Mica/亚克力/关闭），调色板透明度与 backdrop 一起切
  void ApplyWindowMaterial(WindowMaterial material, bool announce);
  // 当前文档查找（Ctrl+F）与工作区搜索（Ctrl+Shift+F）是两个入口
  void FocusDocumentFind();
  void FocusWorkspaceSearch();
  // 活动栏切换侧栏视图；点已选中的项则收起/展开侧栏
  void ShowSideView(SideView view);
  void EnsureSideView(SideView view);
  // 只切活动栏状态、标题与侧栏页，不碰可见性和持久化
  void ApplySideView(SideView view);
  void UpdateOutlineVisibility();
  void ApplySplitterSizes();
  void FindInDocument(const QString& query, bool backward);
  void UpdateFindControls();
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
  void UpdateMaterialActions();
  // FluentUI 局部 palette 不会随 qApp->setPalette 自动刷新，切主题后
  // 显式把新调色板压给外壳控件和正文页
  void RefreshChromePalette();
  // 主题图标两套（svg/light、svg/dark）：换肤后整体换一套并同步到
  // 侧栏/欢迎页/快速跳转
  void RefreshThemeIcons();
  bool ThemeDark() const;
  // 阅读区样式 opt-in：原型 CSS 或库默认；同时刷新页面背景
  void ApplyReadingStyle();
  void ApplyReadingChromeTo(MarkdownView* view);
  void UpdateReadingSurface();
  // 阅读外观（字号/行距/栏宽）：偏好 CSS 以追加层注入，不改基准主题
  ReaderPreferences CurrentReaderPreferences() const;
  void ApplyReaderAppearance();
  void ShowReaderAppearance();
  void ShowSettings();
  // 无文档时的欢迎页，与正文互斥显示
  void ShowWelcome();
  void HideWelcome();
  // 顶部进度线、浮动回顶按钮与状态栏阅读时长
  void UpdateReadingProgress(int y);
  // 等 qlitehtml 完成重排后再按新高度重算进度
  void ScheduleReadingProgressRefresh();
  void UpdateReadingTime();
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
  // 当前标签由 active_tab_ 决定；tabs_[0] 恒为欢迎标签
  DocumentTab* CurrentTab() const;
  MarkdownView* CurrentView() const;
  ReaderPage* CurrentPage() const;
  int FindTabIndex(const QString& path) const;
  DocumentTab* CreateDocumentTab();
  void ClearDocumentTabs();
  // 删除活动标签前先把面板绑回欢迎视图，避免持有悬空 view_ 指针
  void DetachPanelsFromTab(DocumentTab* tab);
  void ActivateTab(int index);
  // 激活下标已改好后刷新页面与外壳；关闭标签后也走这里
  void ShowActiveTab();
  void CloseTab(int index);
  void ReloadTab(int index);
  void ReopenClosedTab();
  void OnTabMoved(int from, int to);
  void UpdateTabTitle(DocumentTab* tab);
  void SetTabLoading(DocumentTab* tab, bool loading);
  void WriteTabSession();
  void SaveTabSession();
  void RefreshRecentRootsMenu();
  void OpenRecentRoot(const QString& root);
  void ApplyGlobalSettings(DocumentTab* tab);
  void CaptureGlobalSettings(DocumentTab* tab);
  void ConnectTabSignals(DocumentTab* tab);
  void UpdateShellForActiveTab();

  // 菜单里挂着；菜单栏在 fluent 壳下会被搬进标题栏
  QAction* open_file_action_ = nullptr;
  QAction* open_dir_action_ = nullptr;
  QAction* new_tab_action_ = nullptr;
  QAction* close_tab_action_ = nullptr;
  QAction* reopen_tab_action_ = nullptr;
  QAction* new_window_action_ = nullptr;
  QAction* unload_workspace_action_ = nullptr;
  QAction* quit_action_ = nullptr;
  QAction* about_action_ = nullptr;
  QAction* export_html_action_ = nullptr;
  QAction* export_pdf_action_ = nullptr;
  QAction* find_action_ = nullptr;
  QAction* workspace_search_action_ = nullptr;
  QAction* view_documents_action_ = nullptr;
  QAction* view_bookmarks_action_ = nullptr;
  QAction* view_render_status_action_ = nullptr;
  QAction* outline_action_ = nullptr;
  QAction* quick_open_action_ = nullptr;
  QAction* back_action_ = nullptr;
  QAction* forward_action_ = nullptr;
  QAction* zoom_in_action_ = nullptr;
  QAction* zoom_out_action_ = nullptr;
  QAction* zoom_reset_action_ = nullptr;
  QAction* settings_action_ = nullptr;
  QAction* bookmark_action_ = nullptr;
  QAction* refresh_action_ = nullptr;
  QAction* material_auto_action_ = nullptr;
  QAction* material_acrylic_action_ = nullptr;
  QAction* material_none_action_ = nullptr;
  // Fluent 标题栏自带的搜索框；控件归标题栏所有
  QLineEdit* document_find_edit_ = nullptr;
  QLineEdit* workspace_search_edit_ = nullptr;
  // 两类搜索的防抖、请求 id 与结果过滤都在这里
  SearchController* search_controller_ = nullptr;

  // 标签集合：tabs_[0] 恒为欢迎标签（path 为空、page 为空）
  std::vector<std::unique_ptr<DocumentTab>> tabs_;
  int active_tab_ = 0;
  // 中心区：第 0 页是欢迎页，之后每页对应一个 ReaderPage
  QStackedWidget* pages_ = nullptr;
  TopToolBar* top_tool_bar_ = nullptr;
  QStackedWidget* bar_stack_ = nullptr;
  WelcomeBar* welcome_bar_ = nullptr;
  MarkdownOutlineWidget* outline_ = nullptr;
  SettingsPanel* settings_panel_ = nullptr;
  RenderStatusPanel* render_status_ = nullptr;
  SidePanel* side_panel_view_ = nullptr;
  QuickOpenPopup* quick_open_ = nullptr;
  ReaderAppearancePopup* appearance_popup_ = nullptr;
  WelcomeView* welcome_ = nullptr;
  QVariantAnimation* scroll_animation_ = nullptr;
  // SetRootDir 时缓存的 Markdown 文件列表；快速跳转不重复扫盘
  QStringList quick_open_files_;
  // 最近关闭的文档路径（最多 10 条），供“重新打开”使用
  QStringList closed_tabs_;
  QLabel* status_ready_label_ = nullptr;
  QLabel* tab_count_label_ = nullptr;
  QLabel* document_count_label_ = nullptr;
  QLabel* reading_time_label_ = nullptr;
  QLabel* backend_label_ = nullptr;
  QProgressBar* render_progress_ = nullptr;
  // 上一次渲染状态：只用来看引擎首次加载与本次失败增量
  MarkdownRendererStatus last_render_status_;
  // 当前文档集的根目录；单文件在根外时才换根
  QString root_dir_;
  std::unique_ptr<BookmarkController> bookmark_controller_;
  std::unique_ptr<ReadingPositionController> position_controller_;
  QSet<QString> large_file_prompted_;
  QFileSystemWatcher* root_watcher_ = nullptr;
  QTimer* root_rescan_timer_ = nullptr;
  QStringList watched_dirs_;
  struct PendingJump {
    QString path;
    int ordinal = 0;
    QString query;
  };
  std::optional<PendingJump> pending_search_hit_;

  // 无边框窗口壳（QWK + Fluent 标题栏）
  FluentWindowFrame* fluent_frame_ = nullptr;
  SettingsDialog* settings_dialog_ = nullptr;
  ActivityBar* activity_bar_ = nullptr;
  QWidget* side_panel_ = nullptr;
  QLabel* side_title_ = nullptr;
  QStackedWidget* side_stack_ = nullptr;
  QWidget* render_status_page_ = nullptr;
  // 活动栏视图到侧栏页的映射：新增视图只改这张表，不在切换逻辑里加分支
  std::array<QWidget*, kSideViewCount> side_view_pages_{};
  QWidget* outline_column_ = nullptr;
  QSplitter* main_splitter_ = nullptr;
  int side_width_ = 280;
  int outline_width_ = 212;
  QToolButton* outline_hide_button_ = nullptr;
  // 用户是否希望看到大纲；窄窗自动隐藏不改这个值
  bool outline_user_visible_ = true;
  QWidget* title_leading_widget_ = nullptr;
  DocumentBar* document_bar_ = nullptr;
  DocumentTabBar* tab_bar_ = nullptr;
  QToolButton* menu_button_ = nullptr;
  QMenu* app_menu_ = nullptr;
  QMenu* view_menu_ = nullptr;
  WindowMaterial window_material_ = WindowMaterial::kAuto;

  // 标准配置目录下的 md_viewer.ini，归 main 持有，生命周期长于本窗口
  MdViewerSettings* settings_ = nullptr;
  // 次窗口的设置副本（只读）；settings_ 指向它
  std::unique_ptr<MdViewerSettings> owned_settings_;
  QMenu* recent_menu_ = nullptr;
  QMenu* recent_roots_menu_ = nullptr;
  std::unique_ptr<SessionController> session_controller_;
  bool tab_switch_queued_ = false;
};

#endif  // FILE_APPS_QT_MD_VIEWER_MD_VIEWER_WINDOW_H_
