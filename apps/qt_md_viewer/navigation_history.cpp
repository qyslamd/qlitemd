#include "navigation_history.h"

void NavigationHistory::Push(const NavigationEntry& entry) {
  while (entries_.size() > index_ + 1) {
    entries_.removeLast();
  }
  // 同一目标重复压栈只会让“后退”按了没反应，直接吞掉
  if (entries_.isEmpty() || !(entries_.last() == entry)) {
    entries_.append(entry);
  }
  index_ = entries_.size() - 1;
}

bool NavigationHistory::CanGoBack() const { return index_ > 0; }

bool NavigationHistory::CanGoForward() const {
  return index_ >= 0 && index_ + 1 < entries_.size();
}

std::optional<NavigationEntry> NavigationHistory::Back() {
  if (!CanGoBack()) {
    return std::nullopt;
  }
  --index_;
  return entries_.at(index_);
}

std::optional<NavigationEntry> NavigationHistory::Forward() {
  if (!CanGoForward()) {
    return std::nullopt;
  }
  ++index_;
  return entries_.at(index_);
}

int NavigationHistory::size() const { return entries_.size(); }
