#pragma once

#include <QFrame>
#include <QPointer>

class ExInfoBarHost;

class PageFeedback final : public QFrame
{
    Q_OBJECT

public:
    explicit PageFeedback( QWidget* parent = nullptr );

private:
    QPointer<ExInfoBarHost> m_infoBarHost;
    int m_popupSerial = 0;
};
