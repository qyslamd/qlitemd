#pragma once

#include <QFrame>

class QEvent;

class PageAudioLevelMeter final : public QFrame
{
    Q_OBJECT

public:
    explicit PageAudioLevelMeter( QWidget* parent = nullptr );

protected:
    void changeEvent( QEvent* event ) override;
};
