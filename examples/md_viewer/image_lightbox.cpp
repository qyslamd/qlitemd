#include "image_lightbox.h"

#include <QGraphicsPixmapItem>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QShowEvent>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWheelEvent>

namespace {

constexpr qreal kMinScale = 0.05;
constexpr qreal kMaxScale = 20.0;

class LightboxView final : public QGraphicsView {
 public:
  LightboxView(QGraphicsPixmapItem* item, QWidget* parent = nullptr)
      : QGraphicsView(parent), item_(item) {
    setDragMode(QGraphicsView::ScrollHandDrag);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setResizeAnchor(QGraphicsView::AnchorViewCenter);
    setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
    setFrameShape(QFrame::NoFrame);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setBackgroundBrush(Qt::NoBrush);
    setAttribute(Qt::WA_TranslucentBackground);
    viewport()->setAutoFillBackground(false);
  }

  void FitImage() {
    if (item_ != nullptr) {
      fitInView(item_, Qt::KeepAspectRatio);
    }
    fit_mode_ = true;
  }

  void ActualSize() {
    resetTransform();
    if (item_ != nullptr) {
      centerOn(item_);
    }
    fit_mode_ = false;
  }

 protected:
  void wheelEvent(QWheelEvent* event) override {
    const int delta = event->angleDelta().y();
    if (delta == 0) {
      // 横向触控板滚动交给基类，别当成放大
      event->ignore();
      return;
    }
    const qreal factor = delta > 0 ? 1.15 : 1.0 / 1.15;
    const qreal current = transform().m11();
    // FitImage 可能把很小的图放大到 20 倍以上：上限只能拦继续放大，
    // 不能拦住缩小，否则滚轮在小图上完全失效。
    if ((factor > 1.0 && current >= kMaxScale) ||
        (factor < 1.0 && current <= kMinScale)) {
      event->accept();
      return;
    }
    scale(factor, factor);
    fit_mode_ = false;
    event->accept();
  }

  void mouseDoubleClickEvent(QMouseEvent* event) override {
    if (event->button() == Qt::LeftButton) {
      if (fit_mode_) {
        ActualSize();
      } else {
        FitImage();
      }
      event->accept();
      return;
    }
    QGraphicsView::mouseDoubleClickEvent(event);
  }

  void resizeEvent(QResizeEvent* event) override {
    QGraphicsView::resizeEvent(event);
    if (fit_mode_) {
      FitImage();
    }
  }

 private:
  QGraphicsPixmapItem* item_ = nullptr;
  bool fit_mode_ = true;
};

QToolButton* AddActionButton(QHBoxLayout* layout, const QString& text,
                             const QString& object_name, QWidget* parent) {
  auto* button = new QToolButton(parent);
  button->setObjectName(object_name);
  button->setText(text);
  button->setAutoRaise(true);
  button->setMinimumHeight(30);
  button->setCursor(Qt::PointingHandCursor);
  layout->addWidget(button);
  return button;
}

QColor LightText() { return QColor(238, 240, 245); }

}  // namespace

ImageLightbox::ImageLightbox(const QPixmap& pixmap, const QString& title,
                             bool has_source, QWidget* parent)
    : QDialog(parent) {
  setObjectName(QStringLiteral("imageLightbox"));
  setWindowTitle(title);
  setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
  setAttribute(Qt::WA_TranslucentBackground);
  setModal(true);
  setFocusPolicy(Qt::StrongFocus);

  QPalette dark_palette = palette();
  dark_palette.setColor(QPalette::WindowText, LightText());
  dark_palette.setColor(QPalette::ButtonText, LightText());
  dark_palette.setColor(QPalette::PlaceholderText, QColor(168, 176, 190));
  setPalette(dark_palette);

  auto* outer = new QVBoxLayout(this);
  outer->setContentsMargins(28, 18, 28, 18);
  outer->setSpacing(10);

  auto* top = new QHBoxLayout;
  top->setSpacing(6);
  title_label_ = new QLabel(title, this);
  title_label_->setObjectName(QStringLiteral("imageLightboxTitle"));
  QFont title_font = font();
  if (title_font.pointSizeF() > 0.0) {
    title_font.setPointSizeF(title_font.pointSizeF() + 2.0);
  }
  title_font.setWeight(QFont::DemiBold);
  title_label_->setFont(title_font);
  top->addWidget(title_label_);
  top->addStretch(1);

  auto* fit = AddActionButton(top, QStringLiteral("适应窗口"),
                              QStringLiteral("imageLightboxFit"), this);
  auto* actual = AddActionButton(top, QStringLiteral("100%"),
                                 QStringLiteral("imageLightboxActual"), this);
  auto* source = AddActionButton(top, QStringLiteral("源码"),
                                 QStringLiteral("imageLightboxSource"), this);
  auto* save = AddActionButton(top, QStringLiteral("另存为"),
                               QStringLiteral("imageLightboxSave"), this);
  auto* close = AddActionButton(top, QStringLiteral("关闭"),
                                QStringLiteral("imageLightboxClose"), this);
  source->setVisible(has_source);
  outer->addLayout(top);

  auto* scene = new QGraphicsScene(this);
  // 公式/Mermaid PNG 的背景是透明的：在暗色灯箱里垫一层浅色纸面，
  // 否则深色文字会消失在背景里
  auto* backing = scene->addRect(QRectF(0, 0, pixmap.width(), pixmap.height()),
                                 Qt::NoPen, QColor(246, 248, 250));
  backing->setZValue(-1);
  item_ = scene->addPixmap(pixmap);
  auto* view = new LightboxView(item_, this);
  view_ = view;
  view->setScene(scene);
  outer->addWidget(view_, 1);

  auto* hint = new QLabel(
      QStringLiteral("滚轮缩放 · 拖动平移 · 双击切换适应/100% · Esc 关闭"),
      this);
  hint->setObjectName(QStringLiteral("imageLightboxHint"));
  hint->setAlignment(Qt::AlignCenter);
  hint->setForegroundRole(QPalette::PlaceholderText);
  QFont hint_font = font();
  if (hint_font.pointSizeF() > 0.0) {
    hint_font.setPointSizeF(qMax(8.0, hint_font.pointSizeF() - 1.0));
  }
  hint->setFont(hint_font);
  outer->addWidget(hint);

  connect(fit, &QToolButton::clicked, this, &ImageLightbox::FitToWindow);
  connect(actual, &QToolButton::clicked, this, &ImageLightbox::ActualSize);
  connect(source, &QToolButton::clicked, this, &ImageLightbox::sourceRequested);
  connect(save, &QToolButton::clicked, this, &ImageLightbox::saveRequested);
  connect(close, &QToolButton::clicked, this, &QDialog::accept);
}

void ImageLightbox::FitToWindow() {
  if (view_ != nullptr) {
    static_cast<LightboxView*>(view_)->FitImage();
  }
}

void ImageLightbox::ActualSize() {
  if (view_ != nullptr) {
    static_cast<LightboxView*>(view_)->ActualSize();
  }
}

void ImageLightbox::paintEvent(QPaintEvent* event) {
  Q_UNUSED(event);
  QPainter painter(this);
  painter.fillRect(rect(), QColor(8, 10, 14, 215));
}

void ImageLightbox::keyPressEvent(QKeyEvent* event) {
  if (event->key() == Qt::Key_Escape) {
    reject();
    return;
  }
  if (event->key() == Qt::Key_0) {
    ActualSize();
    return;
  }
  if (event->key() == Qt::Key_F) {
    FitToWindow();
    return;
  }
  QDialog::keyPressEvent(event);
}

void ImageLightbox::showEvent(QShowEvent* event) {
  if (!fitted_once_) {
    fitted_once_ = true;
    QWidget* host =
        parentWidget() != nullptr ? parentWidget()->window() : nullptr;
    if (host != nullptr) {
      setGeometry(host->frameGeometry());
    }
    QTimer::singleShot(0, this, [this] {
      FitToWindow();
      if (view_ != nullptr) {
        view_->setFocus();
      }
    });
  }
  QDialog::showEvent(event);
}
