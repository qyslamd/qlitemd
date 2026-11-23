#ifndef FILE_APPS_QT_MD_VIEWER_READER_PAGE_H_
#define FILE_APPS_QT_MD_VIEWER_READER_PAGE_H_

#include <QWidget>

class QLabel;
class QResizeEvent;
class QToolButton;
class ReadingProgressBar;

// 一个标签的阅读区：进度线 + 面包屑 + 正文 + 浮动回顶按钮。
// 正文由宿主创建后交给本页托管；欢迎页不是 ReaderPage。
class ReaderPage : public QWidget {
  Q_OBJECT

 public:
  explicit ReaderPage(QWidget* content, QWidget* parent = nullptr);

  ReadingProgressBar* progress() const { return progress_; }
  QLabel* breadcrumb() const { return breadcrumb_; }
  QToolButton* back_to_top() const { return back_to_top_; }

 protected:
  void resizeEvent(QResizeEvent* event) override;

 private:
  void PositionBackToTop();

  ReadingProgressBar* progress_ = nullptr;
  QLabel* breadcrumb_ = nullptr;
  QToolButton* back_to_top_ = nullptr;
};

#endif  // FILE_APPS_QT_MD_VIEWER_READER_PAGE_H_
