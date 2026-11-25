#include "side_view.h"

#include <QCoreApplication>

bool IsValidSideView(int value) { return value >= 0 && value < kSideViewCount; }

SideView SideViewFromInt(int value, SideView fallback) {
  return IsValidSideView(value) ? static_cast<SideView>(value) : fallback;
}

QString SideViewTitle(SideView view) {
  // 自由函数没有 tr() 上下文，走全局 translate；context 与类名一致
  switch (view) {
    case SideView::kDocuments:
      return QCoreApplication::translate("SideView", "Documents");
    case SideView::kSearch:
      return QCoreApplication::translate("SideView", "Search");
    case SideView::kBookmarks:
      return QCoreApplication::translate("SideView", "Bookmarks");
    case SideView::kRenderStatus:
      return QCoreApplication::translate("SideView", "Render Status");
  }
  return QCoreApplication::translate("SideView", "Documents");
}
