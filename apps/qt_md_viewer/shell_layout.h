#ifndef FILE_APPS_QT_MD_VIEWER_SHELL_LAYOUT_H_
#define FILE_APPS_QT_MD_VIEWER_SHELL_LAYOUT_H_

#include <algorithm>

// 首次启动/重置布局用的三栏默认宽度。纯函数，方便无头测试。
struct ShellMetrics {
  int sidebar_width = 0;
  int outline_width = 0;
};

inline ShellMetrics DefaultShellMetrics(int total_width,
                                        bool outline_available) {
  const int width = std::max(total_width, 0);
  ShellMetrics metrics;
  if (!outline_available || width < 980) {
    metrics.sidebar_width =
        std::clamp(static_cast<int>(width * 0.32 + 0.5), 220, 300);
    return metrics;
  }
  metrics.sidebar_width =
      std::clamp(static_cast<int>(width * 0.24 + 0.5), 240, 340);
  metrics.outline_width =
      std::clamp(static_cast<int>(width * 0.18 + 0.5), 200, 300);
  return metrics;
}

#endif  // FILE_APPS_QT_MD_VIEWER_SHELL_LAYOUT_H_
