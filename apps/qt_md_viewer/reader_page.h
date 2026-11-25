#ifndef FILE_APPS_QT_MD_VIEWER_READER_PAGE_H_
#define FILE_APPS_QT_MD_VIEWER_READER_PAGE_H_

#include <QWidget>

class QResizeEvent;
class QToolButton;

// 阅读区顶部的 2~3px 进度线。只负责画，进度由宿主按滚动位置计算。
class ReadingProgressBar : public QWidget {
  Q_OBJECT

 public:
  explicit ReadingProgressBar(QWidget* parent = nullptr);

  // 0..1，越界会夹紧
  void SetProgress(qreal progress);
  qreal progress() const { return progress_; }

 protected:
  void paintEvent(QPaintEvent* event) override;

 private:
  qreal progress_ = 0.0;
};

// 一个标签的阅读区：进度线 + 正文 + 浮动回顶按钮。
// 正文由宿主创建后交给本页托管；欢迎页不是 ReaderPage。
class ReaderPage : public QWidget {
  Q_OBJECT

 public:
  explicit ReaderPage(QWidget* content, QWidget* parent = nullptr);

  ReadingProgressBar* progress() const { return progress_; }
  QToolButton* back_to_top() const { return back_to_top_; }

 protected:
  void resizeEvent(QResizeEvent* event) override;

 private:
  void PositionBackToTop();

  ReadingProgressBar* progress_ = nullptr;
  QToolButton* back_to_top_ = nullptr;
};

#endif  // FILE_APPS_QT_MD_VIEWER_READER_PAGE_H_
