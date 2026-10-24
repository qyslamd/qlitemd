#ifndef FILE_TESTS_MARKDOWN_MARKDOWN_TEST_WAIT_H_
#define FILE_TESTS_MARKDOWN_MARKDOWN_TEST_WAIT_H_

#include <QSignalSpy>
#include <QTest>

#include "markdown_view.h"

// 必须先构造 waiter 再触发渲染，否则缓存命中/空文档的同步提交信号会漏掉
class RenderWaiter {
 public:
  explicit RenderWaiter(MarkdownView* view)
      : spy_(view, &MarkdownView::renderFinished) {}

  bool Wait(int timeout_ms = 60000) {
    if (spy_.count() > 0) {
      return true;
    }
    return spy_.wait(timeout_ms);
  }

 private:
  QSignalSpy spy_;
};

#endif  // FILE_TESTS_MARKDOWN_MARKDOWN_TEST_WAIT_H_
