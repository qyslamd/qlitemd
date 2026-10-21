#include "excolorpickerbutton.h"

#include "excolorpicker.h"
#include "exfonticon.h"

#include <QEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QStyleOptionToolButton>
#include <QStylePainter>

namespace
{
constexpr int kContentHMargin = 8;
constexpr int kContentItemHMargin = 5;
constexpr int kSwatchVerticalMargin = 5;

static const QChar kChevronDown = ExFontIcon::iconChar( SegoeIcon::ChevronDown );
} // namespace

ExColorPickerButton::ExColorPickerButton(QWidget *parent)
    : QToolButton(parent)
{
    setToolButtonStyle(Qt::ToolButtonTextOnly);
    setPopupMode(QToolButton::InstantPopup);
    setArrowType(Qt::DownArrow);
    setFixedSize(68, 32);
    connect(this, &QToolButton::clicked, this, &ExColorPickerButton::showPicker);
    syncButtonAppearance();
}

QColor ExColorPickerButton::selectedColor() const
{
    if (m_picker)
        return m_picker->color();
    return m_selectedColor;
}

void ExColorPickerButton::setSelectedColor(const QColor &color)
{
    if (m_picker)
    {
        if (m_picker->color() == color)
            return;
        m_picker->setColor(color);
        m_selectedColor = m_picker->color();
    }
    else
    {
        if (m_selectedColor == color)
            return;
        m_selectedColor = color;
    }
    syncButtonAppearance();
    Q_EMIT selectedColorChanged(m_selectedColor);
}

ExColorPicker *ExColorPickerButton::colorPicker() const
{
    return m_picker;
}

void ExColorPickerButton::paintEvent(QPaintEvent *)
{
    QStylePainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    QStyleOptionToolButton option;
    initStyleOption(&option);
    option.icon = QIcon();
    option.text.clear();
    option.arrowType = Qt::NoArrow;
    option.features &= ~QStyleOptionToolButton::HasMenu;
    painter.drawComplexControl(QStyle::CC_ToolButton, option);

    const int arrowWidth = style()->pixelMetric(QStyle::PM_MenuButtonIndicator, &option, this);
    QRect arrowArea(option.rect.right() - arrowWidth - 2,
                    option.rect.top(),
                    arrowWidth,
                    option.rect.height());

    QRect swatchRect = option.rect.adjusted(kContentHMargin, kSwatchVerticalMargin, -kContentHMargin, -kSwatchVerticalMargin);
    swatchRect.setRight(arrowArea.left() - kContentItemHMargin);

    painter.setPen(Qt::NoPen);
    painter.setBrush(QBrush(m_selectedColor));
    painter.drawRoundedRect(swatchRect, 3, 3);

    painter.setFont(ExFontIcon::iconFont(11));
    const QColor arrowColor = option.state & QStyle::State_Enabled
                                  ? palette().color(QPalette::ButtonText)
                                  : palette().color(QPalette::Disabled, QPalette::ButtonText);
    painter.setPen(arrowColor);

    if (m_pressed)
        arrowArea.setTop(arrowArea.top() + 2);
    painter.drawText(arrowArea, Qt::AlignCenter, kChevronDown);
}

void ExColorPickerButton::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
    {
        m_pressed = true;
        update();
    }
    QToolButton::mousePressEvent(event);
}

void ExColorPickerButton::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && m_pressed)
    {
        m_pressed = false;
        update();
    }
    QToolButton::mouseReleaseEvent(event);
}

bool ExColorPickerButton::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_picker && event->type() == QEvent::Hide)
    {
        m_pressed = false;
        update();
    }
    return QToolButton::eventFilter(watched, event);
}

void ExColorPickerButton::syncButtonAppearance()
{
    setIcon(QIcon());
    setText(QString());
    update();
}

void ExColorPickerButton::ensurePicker()
{
    if (m_picker)
        return;

    m_picker = new ExColorPicker(this, true);
    m_picker->installEventFilter(this);
    m_picker->setColor(m_selectedColor);

    connect(m_picker, &ExColorPicker::colorChanged, this, [this](const QColor &)
            {
                const QColor color = m_picker->color();
                if (m_selectedColor == color)
                    return;
                m_selectedColor = color;
                syncButtonAppearance();
                Q_EMIT selectedColorChanged(color);
            });
}

void ExColorPickerButton::showPicker()
{
    ensurePicker();
    m_picker->showPopup(this);
}
