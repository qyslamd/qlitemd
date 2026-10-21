#ifndef FILE_EXAMPLES_MD_VIEWER_MD_VIEWER_WINDOW_H_
#define FILE_EXAMPLES_MD_VIEWER_MD_VIEWER_WINDOW_H_

#include <QMainWindow>
#include <QPoint>
#include <QString>
#include <QUrl>
#include <QVector>

#include "markdown_types.h"

class QAction;
class QCloseEvent;
class QDragEnterEvent;
class QDragMoveEvent;
class QDropEvent;
class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QMenu;
class QProgressBar;
class QTimer;
struct MdViewerSettings;
class FluentWindowFrame;
class MarkdownOutlineWidget;
class MarkdownView;
class RenderSettingsPanel;
class RenderStatusPanel;

// 独立的 Markdown 查看器：单篇与"整个目录"两种打开方式，目录铺在左侧文件
// 列表里。渲染全交给 src/markdown 的 MarkdownView——本类只管宿主策略：
// 窗口壳、菜单、侧栏、状态栏。
class MdViewerWindow : public QMainWindow {
  Q_OBJECT

 public:
  explicit MdViewerWindow(MdViewerSettings* settings,
                          QWidget* parent = nullptr);
  ~MdViewerWindow() override;

  // 命令行参数进来：目录就铺开侧栏并看第一篇，文件直接看
  void OpenPath(const QString& path);
  // 没有命令行参数时：打开最近记录里第一个仍存在的文件
  void OpenLastFile();

 protected:
  void closeEvent(QCloseEvent* event) override;
  void dragEnterEvent(QDragEnterEvent* event) override;
  void dragMoveEvent(QDragMoveEvent* event) override;
  void dropEvent(QDropEvent* event) override;

 private:
  void createActions();
  void initUi();
  void initSignals();

  // 前进/后退栈里的一项：文件 + 要滚到的锚点（空 = 回到顶部）
  struct HistoryEntry {
    QString path;
    QString fragment;
    bool operator==(const HistoryEntry& other) const {
      return path == other.path && fragment == other.fragment;
    }
  };

  // 统一的导航入口：同文件只滚锚点，不重加载；跨文件才走加载
  void NavigateTo(const QString& path, const QString& fragment,
                  bool record_history);
  // 真正打开路径（文件或目录），返回实际打开的文件；失败返回空
  QString OpenPathCore(const QString& path);
  bool ShowFile(const QString& path);
  void PushHistory(const QString& path, const QString& fragment);
  void ScrollToFragment(const QString& fragment);
  void GoBack();
  void GoForward();
  void UpdateHistoryActions();

  void OpenFileDialog();
  void OpenDirectoryDialog();
  // Fluent 壳与文档主题一起切
  void ApplyTheme(MarkdownTheme theme);
  // 标题栏主题按钮：浅色/暗色之间翻转
  void ToggleTheme();
  // Windows：DWM 窗口模糊开关（调色板透明度与 backdrop 一起切）
  void SetWindowBlur(bool enabled);
  void FocusSearch();
  // incremental=true 是"边打字边找"：从当前命中处继续匹配，不清空重来
  void FindNext(bool backward, bool incremental = false);
  void UpdateSearchActionIcons();
  void ExportHtml();
  void ExportPdf();
  // 图片左键：公式/Mermaid 看源码，普通图片预览
  void OpenImage(const QUrl& url);
  void ShowImageContextMenu(const QPoint& global_pos, const QUrl& url);
  void SaveImageAs(const QUrl& url);
  void ShowImagePreview(const QUrl& url);
  void ShowImageSource(const QUrl& url);
  // 只填列表，不加载——加载由 ShowFile 负责，免得两边互相触发。
  // 目录里没有 Markdown 时返回 false，保留原列表与当前文档
  bool RefreshDirectoryListing(const QString& dir);
  void SelectInList(const QString& path);
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
  QAction* find_next_action_ = nullptr;
  QAction* find_prev_action_ = nullptr;
  QAction* back_action_ = nullptr;
  QAction* forward_action_ = nullptr;
  QAction* zoom_in_action_ = nullptr;
  QAction* zoom_out_action_ = nullptr;
  QAction* zoom_reset_action_ = nullptr;
  // 搜索框内的模式开关（挂在 Fluent 标题栏的 QLineEdit 上）
  QAction* case_action_ = nullptr;
  QAction* word_action_ = nullptr;
  // Fluent 标题栏自带的搜索框；控件归标题栏所有
  QLineEdit* search_edit_ = nullptr;
  // 输入防抖：连续打字只搜最后一档
  QTimer* search_timer_ = nullptr;

  MarkdownView* view_ = nullptr;
  MarkdownOutlineWidget* outline_ = nullptr;
  RenderSettingsPanel* render_settings_ = nullptr;
  RenderStatusPanel* render_status_ = nullptr;
  QListWidget* file_list_ = nullptr;
  QLabel* path_label_ = nullptr;
  QLabel* zoom_label_ = nullptr;
  QProgressBar* render_progress_ = nullptr;
  quint64 progress_request_ = 0;
  // 上一次渲染状态：只用来看引擎首次加载与本次失败增量
  MarkdownRendererStatus last_render_status_;
  // 列表当前列的是哪个目录（ShowFile 据此决定要不要重扫）
  QString listed_dir_;
  // 文件跳转与锚点跳转共用的历史栈
  QVector<HistoryEntry> history_;
  int history_index_ = -1;
  // 换文档后要等的锚点；renderFinished 时再滚
  QString pending_fragment_;

  // 无边框窗口壳（QWK + Fluent 标题栏）
  FluentWindowFrame* fluent_frame_ = nullptr;

  // 标准配置目录下的 md_viewer.ini，归 main 持有，生命周期长于本窗口
  MdViewerSettings* settings_ = nullptr;
  QMenu* recent_menu_ = nullptr;
};

#endif  // FILE_EXAMPLES_MD_VIEWER_MD_VIEWER_WINDOW_H_
