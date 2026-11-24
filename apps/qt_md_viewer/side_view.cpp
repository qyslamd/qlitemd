#include "side_view.h"

bool IsValidSideView(int value) { return value >= 0 && value < kSideViewCount; }

SideView SideViewFromInt(int value, SideView fallback) {
  return IsValidSideView(value) ? static_cast<SideView>(value) : fallback;
}

QString SideViewTitle(SideView view) {
  switch (view) {
    case SideView::kDocuments:
      return QStringLiteral("文档");
    case SideView::kSearch:
      return QStringLiteral("搜索");
    case SideView::kBookmarks:
      return QStringLiteral("收藏");
    case SideView::kRenderStatus:
      return QStringLiteral("渲染状态");
  }
  return QStringLiteral("文档");
}
