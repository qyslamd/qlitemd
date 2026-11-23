#ifndef FILE_APPS_QT_MD_VIEWER_DOCUMENT_TAB_H_
#define FILE_APPS_QT_MD_VIEWER_DOCUMENT_TAB_H_

#include <QString>
#include <QVector>

#include "markdown_types.h"
#include "navigation_history.h"

class MarkdownView;

// 一个标签的全部文档级状态。P0 只有一份实例（当前文档），
// P1 起每个标签一份；主题、后端、Dock 等窗口级状态不进来。
struct DocumentTab {
  QString path;                  // 空 = 欢迎页
  MarkdownView* view = nullptr;  // P0 恒非空；P1 起欢迎标签为空
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
