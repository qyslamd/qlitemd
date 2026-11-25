#ifndef FILE_APPS_QT_MD_VIEWER_DOCUMENT_TAB_H_
#define FILE_APPS_QT_MD_VIEWER_DOCUMENT_TAB_H_

#include <memory>

#include <QString>
#include <QVector>

#include "markdown_types.h"
#include "navigation_history.h"

class MarkdownView;
class ReaderPage;

// 一个标签的全部文档级状态。主题、后端、侧栏等窗口级状态不进来。
// ReaderPage 由本结构体独占（析构时删除），MarkdownView 是它的子控件；
// 欢迎标签没有 ReaderPage，只有一个隐藏视图挂在 pages_ 下。
struct DocumentTab {
  ~DocumentTab();

  QString path;                  // 空 = 欢迎页
  MarkdownView* view = nullptr;  // 非拥有；由 page 或 pages_ 持有
  std::unique_ptr<ReaderPage> page;
  NavigationHistory history;
  int scroll_position = 0;
  QVector<MarkdownHeading> outline;
  QString pending_fragment;
  quint64 render_request = 0;
  bool pending_restore = false;
  QString pending_restore_path;
  int zoom_percent = 100;
};

#endif  // FILE_APPS_QT_MD_VIEWER_DOCUMENT_TAB_H_
