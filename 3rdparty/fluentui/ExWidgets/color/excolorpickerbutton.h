#pragma once

#include "exwidgets_global.h"

#include <QColor>
#include <QToolButton>

class QMouseEvent;

class ExColorPicker;

/**
 * @brief 带 Flyout 的颜色选择按钮（对应 CommunityToolkit ColorPickerButton）。
 *
 * 按钮 Content 显示当前颜色，点击弹出 ExColorPicker 飞层。
 */
class EXWIDGETS_EXPORT ExColorPickerButton : public QToolButton
{
    Q_OBJECT
    Q_PROPERTY(QColor selectedColor READ selectedColor WRITE setSelectedColor NOTIFY selectedColorChanged FINAL)

public:
    explicit ExColorPickerButton(QWidget *parent = nullptr);

    QColor selectedColor() const;
    void setSelectedColor(const QColor &color);

    ExColorPicker *colorPicker() const;

Q_SIGNALS:
    void selectedColorChanged(const QColor &color);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void syncButtonAppearance();
    void ensurePicker();
    void showPicker();

    ExColorPicker *m_picker = nullptr;
    QColor m_selectedColor = Qt::blue;
    bool m_pressed = false;
};
