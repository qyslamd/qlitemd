#include "playerwindow.h"

#include "audiomaticplayerwidget.h"

#ifdef AUDIOMATIC_ENABLE_FRAMELESS
#include <fluenttitlebar.h>
#include <fluentwindowframe.h>
#include <QApplication>
#include <QLineEdit>
#include <QToolButton>
#ifdef HAS_FLUENTUI3_STYLE_LIB
#include "fluentui3style.h"
#endif
#endif

#include <QMenuBar>

PlayerWindow::PlayerWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setObjectName(QStringLiteral("AudiomaticMini"));
    setAttribute(Qt::WA_DontCreateNativeAncestors);
    resize(420, 680);
    setMinimumSize(380, 560);

    auto *menuBar = new QMenuBar(this);
    menuBar->setObjectName(QStringLiteral("win-menu-bar"));
    menuBar->hide();

#ifdef AUDIOMATIC_ENABLE_FRAMELESS
    m_windowFrame = new FluentWindowFrame(this, this);
    m_windowFrame->installChromeHeader(menuBar);
    setupTitleBar();
#else
    setMenuBar(menuBar);
#endif

    m_playerWidget = new AudiomaticPlayerWidget(this);
    setCentralWidget(m_playerWidget);

    setWindowTitle(tr("Audiomatic Mini"));
}

PlayerWindow::~PlayerWindow() = default;

#ifdef AUDIOMATIC_ENABLE_FRAMELESS
void PlayerWindow::setupTitleBar()
{
    if (FluentTitleBar *titleBar = m_windowFrame->titleBar())
    {
        titleBar->searchLineEdit()->hide();
        titleBar->themeButton()->hide();
        titleBar->pinButton()->hide();
        connect(titleBar,
                &FluentTitleBar::accentColorChanged,
                this,
                [](const QColor &color) {
                    qApp->setProperty("_q_accent_color",
                                      color.isValid() ? QVariant(color) : QVariant());
#ifdef Q_OS_WIN
                    qApp->setStyle(QStringLiteral("FluentUI3"));
#elif defined(HAS_FLUENTUI3_STYLE_LIB)
                    qApp->setStyle(new FluentUI3Style);
#endif
                });
    }
}
#endif
