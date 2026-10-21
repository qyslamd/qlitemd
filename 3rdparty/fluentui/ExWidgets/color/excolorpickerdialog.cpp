#include "excolorpickerdialog.h"

#include "excolorpicker.h"

#include <QEvent>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPointer>
#include <QPushButton>
#include <QVBoxLayout>

namespace
{
constexpr int kPadding = 16;
constexpr int kButtonHeight = 40;
}

ExColorPickerDialog::ExColorPickerDialog(QWidget *parent)
    : QDialog(parent)
{
    setObjectName(QStringLiteral("exColorPickerDialog"));
    setWindowFlag(Qt::WindowContextHelpButtonHint, false);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto *body = new QVBoxLayout;
    body->setContentsMargins(kPadding, kPadding, kPadding, kPadding);
    body->setSpacing(16);
    m_titleLabel = new QLabel(this);
    m_titleLabel->setObjectName(QStringLiteral("exColorPickerDialogTitle"));
    m_titleLabel->setTextFormat(Qt::PlainText);
    m_titleLabel->setWordWrap(true);
    QFont titleFont = m_titleLabel->font();
    titleFont.setPixelSize(20);
    titleFont.setWeight(QFont::DemiBold);
    m_titleLabel->setFont(titleFont);
    body->addWidget(m_titleLabel);

    m_picker = new ExColorPicker(this);
    // The inline/flyout picker has a fixed size by default. Let this instance
    // fill the dialog while preserving the minimum space its controls need.
    m_picker->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
    m_picker->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_picker->layout()->setContentsMargins(0, 0, 0, 0);
    m_picker->installEventFilter(this);
    body->addWidget(m_picker, 1);
    root->addLayout(body, 1);

    m_footer = new QWidget(this);
    m_footer->setObjectName(QStringLiteral("exColorPickerDialogFooter"));
    auto *buttons = new QHBoxLayout(m_footer);
    buttons->setContentsMargins(kPadding, kPadding, kPadding, kPadding);
    buttons->setSpacing(8);
    m_acceptButton = new QPushButton(tr("确定"), m_footer);
    m_acceptButton->setObjectName(QStringLiteral("exColorPickerDialogAccept"));
    m_acceptButton->setProperty("accent", true);
    m_acceptButton->setDefault(true);
    m_cancelButton = new QPushButton(tr("取消"), m_footer);
    m_cancelButton->setObjectName(QStringLiteral("exColorPickerDialogCancel"));
    m_cancelButton->setAutoDefault(true);
    for (auto *button : {m_acceptButton, m_cancelButton})
    {
        button->setMinimumSize(120, kButtonHeight);
        button->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        buttons->addWidget(button, 1);
    }
    root->addWidget(m_footer);

    connect(this, &QWidget::windowTitleChanged, m_titleLabel, &QLabel::setText);
    connect(m_picker, &ExColorPicker::colorChanged, this, &ExColorPickerDialog::colorChanged);
    connect(m_acceptButton, &QPushButton::clicked, this, &QDialog::accept);
    connect(m_cancelButton, &QPushButton::clicked, this, &QDialog::reject);
    setTitle(tr("编辑颜色"));
    m_initialColor = color();
    resize(sizeHint());
}

ExColorPickerDialog::~ExColorPickerDialog() = default;

QString ExColorPickerDialog::title() const
{
    return windowTitle();
}

void ExColorPickerDialog::setTitle(const QString &title)
{
    setWindowTitle(title);
}

QColor ExColorPickerDialog::color() const
{
    return m_picker->color();
}

void ExColorPickerDialog::setColor(const QColor &color)
{
    if (!color.isValid())
        return;
    QColor normalized = color;
    if (!m_picker->isAlphaEnabled())
        normalized.setAlpha(255);
    if (normalized == m_picker->color())
        return;
    QPointer<ExColorPickerDialog> guard(this);
    m_picker->setColor(normalized);
    if (guard && !isVisible())
        m_initialColor = m_picker->color();
}

ExColorPicker *ExColorPickerDialog::colorPicker() const
{
    return m_picker;
}

QSize ExColorPickerDialog::sizeHint() const
{
    return QDialog::sizeHint().expandedTo(QSize(480, 0));
}

void ExColorPickerDialog::setVisible(bool visible)
{
    if (visible && !isVisible())
        m_initialColor = color();
    QDialog::setVisible(visible);
}

void ExColorPickerDialog::done(int result)
{
    // Preview/selection callbacks may call accept() or reject() again.
    if (m_finishing)
        return;
    m_finishing = true;
    // Commit pending text edits before reading the accepted color, or before
    // restoring the opening color on cancellation.
    QPointer<ExColorPickerDialog> guard(this);
    if (QWidget *focused = focusWidget())
        focused->clearFocus();
    if (!guard)
        return;
    if (result != QDialog::Accepted)
        setColor(m_initialColor);
    if (!guard)
        return;
    const QColor selected = color();
    QDialog::done(result);
    if (!guard)
        return;
    if (result == QDialog::Accepted)
        Q_EMIT colorSelected(selected);
    if (guard)
        m_finishing = false;
}

bool ExColorPickerDialog::eventFilter(QObject *watched, QEvent *event)
{
    // ExColorPicker inherits QDialog even when embedded. Route unhandled
    // Enter/Escape presses to the outer dialog rather than hiding the picker.
    if (watched == m_picker && event->type() == QEvent::KeyPress)
    {
        auto *keyEvent = static_cast<QKeyEvent *>(event);
        if (keyEvent->key() == Qt::Key_Escape || keyEvent->key() == Qt::Key_Return
            || keyEvent->key() == Qt::Key_Enter)
        {
            QDialog::keyPressEvent(keyEvent);
            return true;
        }
    }
    return QDialog::eventFilter(watched, event);
}

void ExColorPickerDialog::changeEvent(QEvent *event)
{
    QDialog::changeEvent(event);
    if (event->type() == QEvent::LanguageChange)
    {
        m_acceptButton->setText(tr("确定"));
        m_cancelButton->setText(tr("取消"));
    }
}
