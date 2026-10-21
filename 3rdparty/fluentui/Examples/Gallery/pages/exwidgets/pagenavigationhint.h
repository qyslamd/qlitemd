#pragma once

#include <QFrame>
#include <QList>
#include <QPointer>
#include <QString>

class ExTeachingTip;
class ExTour;
class QLabel;
class QScrollArea;

class PageNavigationHint final : public QFrame
{
    Q_OBJECT
public:
    explicit PageNavigationHint( QWidget* parent = nullptr );
protected:
    void hideEvent( QHideEvent* event ) override;
private:
    void showTourStep( int index );
    void stopTour( bool completed = false );

    struct TourStep
    {
        QPointer<QWidget> target;
        QString title;
        QString description;
    };
    QList<TourStep> m_tourSteps;
    QPointer<ExTeachingTip> m_tourTip;
    QScrollArea* m_scroll = nullptr;
    QLabel* m_tourStatus = nullptr;
    int m_tourStep = -1;
    QPointer<ExTeachingTip> m_tip;
    ExTour* m_driverTour = nullptr;
};

