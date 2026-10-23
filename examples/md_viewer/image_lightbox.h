#ifndef FILE_EXAMPLES_MD_VIEWER_IMAGE_LIGHTBOX_H_
#define FILE_EXAMPLES_MD_VIEWER_IMAGE_LIGHTBOX_H_

#include <QDialog>
#include <QPixmap>

class QGraphicsPixmapItem;
class QGraphicsView;
class QKeyEvent;
class QLabel;
class QPaintEvent;
class QShowEvent;

// 覆盖主窗口的沉浸式图片查看器：滚轮缩放、拖动平移、双击切换
// 适应/100%。公式/Mermaid 额外显示“源码”入口，动作通过信号交给宿主。
class ImageLightbox : public QDialog {
  Q_OBJECT

 public:
  ImageLightbox(const QPixmap& pixmap, const QString& title, bool has_source,
                QWidget* parent = nullptr);

  void FitToWindow();
  void ActualSize();

 signals:
  void sourceRequested();
  void saveRequested();

 protected:
  void paintEvent(QPaintEvent* event) override;
  void keyPressEvent(QKeyEvent* event) override;
  void showEvent(QShowEvent* event) override;

 private:
  QGraphicsView* view_ = nullptr;
  QGraphicsPixmapItem* item_ = nullptr;
  QLabel* title_label_ = nullptr;
  bool fitted_once_ = false;
};

#endif  // FILE_EXAMPLES_MD_VIEWER_IMAGE_LIGHTBOX_H_
