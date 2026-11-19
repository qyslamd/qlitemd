#ifndef FILE_EXAMPLES_MD_VIEWER_READING_PROGRESS_H_
#define FILE_EXAMPLES_MD_VIEWER_READING_PROGRESS_H_

#include <QWidget>

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

#endif  // FILE_EXAMPLES_MD_VIEWER_READING_PROGRESS_H_
