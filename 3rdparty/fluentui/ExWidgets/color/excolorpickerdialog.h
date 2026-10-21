#pragma once

#include "exwidgets_global.h"

#include <QColor>
#include <QDialog>

class ExColorPicker;
class QLabel;
class QPushButton;

/**
 * Color picker dialog with a title and two full-width, equal-sized footer buttons.
 * colorChanged provides live previews; colorSelected is emitted only on acceptance.
 * Rejecting the dialog restores the color it had when opened.
 */
class EXWIDGETS_EXPORT ExColorPickerDialog : public QDialog
{
    Q_OBJECT
    Q_PROPERTY(QString title READ title WRITE setTitle)
    Q_PROPERTY(QColor color READ color WRITE setColor NOTIFY colorChanged)

public:
    explicit ExColorPickerDialog(QWidget *parent = nullptr);
    ~ExColorPickerDialog() override;

    QString title() const;
    void setTitle(const QString &title);

    QColor color() const;
    void setColor(const QColor &color);

    // Configure alpha, spectrum shape and other options on the embedded picker.
    ExColorPicker *colorPicker() const;

    QSize sizeHint() const override;
    void setVisible(bool visible) override;

public Q_SLOTS:
    void done(int result) override;

Q_SIGNALS:
    void colorChanged(const QColor &color);
    void colorSelected(const QColor &color);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void changeEvent(QEvent *event) override;

private:
    ExColorPicker *m_picker = nullptr;
    QLabel *m_titleLabel = nullptr;
    QWidget *m_footer = nullptr;
    QPushButton *m_acceptButton = nullptr;
    QPushButton *m_cancelButton = nullptr;
    QColor m_initialColor;
    bool m_finishing = false;
};
