#ifndef FILE_APPS_QT_MD_OBSERVATORY_TITLE_BAR_H_
#define FILE_APPS_QT_MD_OBSERVATORY_TITLE_BAR_H_

#include <QAbstractButton>
#include <QWidget>

class QLabel;

// Caption button painted by hand: the app does not use an icon font and does
// not ship window-chrome assets.
class TitleBarButton : public QAbstractButton {
 public:
  enum class Icon { kMinimize, kMaximize, kRestore, kClose, kTheme };

  explicit TitleBarButton(Icon icon, QWidget* parent = nullptr);

  void SetIcon(Icon icon);

  QSize sizeHint() const override;
  QSize minimumSizeHint() const override;

 protected:
  void paintEvent(QPaintEvent* event) override;
  bool event(QEvent* event) override;

 private:
  Icon icon_;
};

class TitleBar : public QWidget {
  Q_OBJECT

 public:
  explicit TitleBar(QWidget* parent = nullptr);

  void SetRootText(const QString& text);
  void SetDocumentText(const QString& text);
  void SetMaximized(bool maximized);

  TitleBarButton* min_button() const { return min_button_; }
  TitleBarButton* max_button() const { return max_button_; }
  TitleBarButton* close_button() const { return close_button_; }
  TitleBarButton* theme_button() const { return theme_button_; }

 signals:
  void themeToggled();

 protected:
  bool event(QEvent* event) override;
  void paintEvent(QPaintEvent* event) override;

 private:
  QLabel* root_label_ = nullptr;
  QLabel* document_label_ = nullptr;
  TitleBarButton* theme_button_ = nullptr;
  TitleBarButton* min_button_ = nullptr;
  TitleBarButton* max_button_ = nullptr;
  TitleBarButton* close_button_ = nullptr;
};

#endif  // FILE_APPS_QT_MD_OBSERVATORY_TITLE_BAR_H_
