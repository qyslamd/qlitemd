#pragma once

#include <QMargins>
#include <QObject>
#include <QPointer>
#include <QScopedPointer>
#include <QString>
#include <QWidget>

#include "exwidgets_global.h"
#include "exwidgetsmacros.h"

class ExTourPrivate;

struct EXWIDGETS_EXPORT ExTourStep
{
    enum Placement
    {
        Auto,
        Top,
        Bottom,
        Left,
        Right
    };

    QPointer<QWidget> target;
    QString title;
    QString description;
    Placement placement   = Auto;
    QMargins targetMargin = QMargins( 6, 4, 6, 4 );
    qreal targetRadius    = 3.0;
};

/**
 * 漫游引导向导控件（对标 Driver.js / Intro.js / Ant Design Tour）：
 * 全屏半透明暗色遮罩、目标控件聚光灯镂空、丝滑平滑过渡动画、带步骤指示的分步气泡提示。
 */
class EXWIDGETS_EXPORT ExTour final : public QObject
{
    Q_OBJECT

public:
    EXWIDGETS_DECLARE_PROPERTY_D( bool, closeOnMaskClick, closeOnMaskClick, setCloseOnMaskClick )
    EXWIDGETS_DECLARE_PROPERTY_D( bool, animated, isAnimated, setAnimated )

    explicit ExTour( QObject* parent = nullptr );
    ~ExTour() override;

    void addStep( const ExTourStep& step );
    void addStep( QWidget* target, const QString& title, const QString& description, ExTourStep::Placement placement = ExTourStep::Auto );
    void setSteps( const QList<ExTourStep>& steps );
    QList<ExTourStep> steps() const;

    int currentStep() const;
    int stepCount() const;
    bool isRunning() const;

public Q_SLOTS:
    void start( int initialIndex = 0 );
    void next();
    void previous();
    void exit();

Q_SIGNALS:
    void stepChanged( int index );
    void finished();
    void cancelled();

private:
    QScopedPointer<ExTourPrivate> d_ptr;
    Q_DECLARE_PRIVATE( ExTour )
};
