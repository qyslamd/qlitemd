#ifndef FILE_APPS_QT_MD_OBSERVATORY_COMMAND_PALETTE_H_
#define FILE_APPS_QT_MD_OBSERVATORY_COMMAND_PALETTE_H_

#include <functional>

#include <QVector>
#include <QWidget>

class QFrame;
class QLineEdit;
class QListWidget;

struct PaletteItem {
  QString category;
  QString title;
  QString detail;
  std::function<void()> action;
};

// Overlay palette for documents, sections and commands. It is a plain child
// widget so it works with the app's own palette and keys, without a dialog.
class CommandPalette : public QWidget {
  Q_OBJECT

 public:
  explicit CommandPalette(QWidget* parent = nullptr);

  void SetItems(QVector<PaletteItem> items);
  void SetDark(bool dark);
  void Open();
  void Close();
  bool is_open() const;

 signals:
  void closed();

 protected:
  bool eventFilter(QObject* watched, QEvent* event) override;
  void paintEvent(QPaintEvent* event) override;
  void resizeEvent(QResizeEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;

 private:
  void Rebuild(const QString& filter);
  void ActivateCurrent();

  QFrame* card_ = nullptr;
  QLineEdit* input_ = nullptr;
  QListWidget* list_ = nullptr;
  QVector<PaletteItem> items_;
  QVector<int> filtered_;
};

#endif  // FILE_APPS_QT_MD_OBSERVATORY_COMMAND_PALETTE_H_
