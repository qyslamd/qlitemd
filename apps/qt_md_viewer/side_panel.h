#ifndef FILE_APPS_QT_MD_VIEWER_SIDE_PANEL_H_
#define FILE_APPS_QT_MD_VIEWER_SIDE_PANEL_H_

#include <QString>
#include <QVector>
#include <QWidget>

#include "bookmarks.h"
#include "document_index.h"
#include "side_panel_roles.h"
#include "side_view.h"
#include "text_search.h"

class QLabel;
class QLineEdit;
class QStackedWidget;
class DocumentsPage;
class BookmarksPage;
class SearchPage;

// 左侧栏：标题 + 一个按 SideView 数值排布的 QStackedWidget。
// 文档/搜索/书签三页由本类创建，渲染状态页由宿主注入（避免侧栏依赖
// 具体的渲染面板）；根目录、收藏集合、搜索任务都由 MdViewerWindow 编排。
class SidePanel : public QWidget {
  Q_OBJECT

 public:
  explicit SidePanel(QWidget* parent = nullptr);

  // 渲染状态页：SidePanel 负责提供带边距的宿主页，控件归宿主所有
  void SetRenderStatusWidget(QWidget* widget);

  void SetRoot(const DocumentScanResult& result);
  void SetRootMissing(const QString& root, const QString& reason);
  void SetFilter(const QString& text);
  // 没有工作区时禁用搜索输入，占位文字改成提示
  void SetSearchAvailable(bool available);
  QLineEdit* search_edit() const;
  void SetCurrentFile(const QString& absolute_path);
  void SetBookmarks(const QVector<Bookmark>& bookmarks,
                    const QString& current_root);
  void BeginSearch(const QString& query);
  void AppendSearchHits(const QVector<SearchHit>& hits, int scanned_files,
                        int total_files);
  void EndSearch(bool truncated, bool canceled);
  void SetSearchError(const QString& message);
  // 四页与 SideView 一一对应：页下标就是枚举值
  void ShowView(SideView view);
  SideView currentView() const;
  // 暗色/亮色两套 SVG 图标：切主题后由窗口调用
  void SetThemeDark(bool dark);

 signals:
  // new_tab：Ctrl+点击或中键；普通点击替换当前标签
  void fileActivated(const QString& absolute_path, bool new_tab);
  void bookmarkActivated(const QString& absolute_path);
  void bookmarkRemoveRequested(const QString& absolute_path);
  // 文档树行内星标：切换该文件的收藏状态
  void bookmarkToggleRequested(const QString& absolute_path);
  void searchTextChanged(const QString& text);
  void searchSubmitted();
  void searchHitActivated(const SearchHit& hit);
  void cancelSearchRequested();
  void chooseRootRequested();

 private:
  void initUi();
  void initSignals();

  QLabel* title_ = nullptr;
  QStackedWidget* stack_ = nullptr;
  DocumentsPage* documents_ = nullptr;
  SearchPage* search_ = nullptr;
  BookmarksPage* bookmarks_ = nullptr;
  QWidget* render_status_host_ = nullptr;
};

#endif  // FILE_APPS_QT_MD_VIEWER_SIDE_PANEL_H_
