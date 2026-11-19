#ifndef FILE_EXAMPLES_MD_VIEWER_NAVIGATION_HISTORY_H_
#define FILE_EXAMPLES_MD_VIEWER_NAVIGATION_HISTORY_H_

#include <optional>

#include <QString>
#include <QVector>

struct NavigationEntry {
  QString path;
  QString fragment;

  bool operator==(const NavigationEntry& other) const {
    return path == other.path && fragment == other.fragment;
  }
};

// 纯数据的前进/后退栈。窗口只负责把“同文件锚点”与“跨文件跳转”都压进来，
// 不在这里区分两者的语义。
class NavigationHistory {
 public:
  void Push(const NavigationEntry& entry);
  bool CanGoBack() const;
  bool CanGoForward() const;
  std::optional<NavigationEntry> Back();
  std::optional<NavigationEntry> Forward();
  int size() const;

 private:
  QVector<NavigationEntry> entries_;
  int index_ = -1;
};

#endif  // FILE_EXAMPLES_MD_VIEWER_NAVIGATION_HISTORY_H_
