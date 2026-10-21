#pragma once

#include "exwidgets_global.h"
#include "exwidgetsmacros.h"

#include <QPixmap>
#include <QScopedPointer>
#include <QWidget>
#include <QtGlobal>

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
class QEnterEvent;
#endif

class ExCarouselPrivate;

/**
 * 水平轮播图：可放任意 QWidget 或图片，支持自动播放、循环、指示点和两侧切换按钮。
 */
class EXWIDGETS_EXPORT ExCarousel : public QWidget
{
    Q_OBJECT

public:
    enum NavigationButtonTrigger
    {
        AlwaysVisible,
        OnHover
    };
    Q_ENUM(NavigationButtonTrigger)

    Q_PROPERTY(int count READ count)

    EXWIDGETS_DECLARE_PROPERTY_D_NOTIFY(int, currentIndex, currentIndex, setCurrentIndex, currentIndexChanged)
    EXWIDGETS_DECLARE_PROPERTY_D_NOTIFY(bool, autoPlay, autoPlay, setAutoPlay, autoPlayChanged)
    EXWIDGETS_DECLARE_PROPERTY_D_NOTIFY(int, interval, interval, setInterval, intervalChanged)
    EXWIDGETS_DECLARE_PROPERTY_D_NOTIFY(bool, wrap, wrap, setWrap, wrapChanged)
    EXWIDGETS_DECLARE_PROPERTY_D_NOTIFY(bool, showNavigationButtons, showNavigationButtons, setShowNavigationButtons, showNavigationButtonsChanged)
    EXWIDGETS_DECLARE_PROPERTY_D_NOTIFY(bool, showIndicators, showIndicators, setShowIndicators, showIndicatorsChanged)
    EXWIDGETS_DECLARE_PROPERTY_D_NOTIFY(int, animationDuration, animationDuration, setAnimationDuration, animationDurationChanged)
    EXWIDGETS_DECLARE_PROPERTY_D_NOTIFY(bool, pauseOnHover, pauseOnHover, setPauseOnHover, pauseOnHoverChanged)
    EXWIDGETS_DECLARE_PROPERTY_D_NOTIFY(qreal, borderRadius, borderRadius, setBorderRadius, borderRadiusChanged)
    EXWIDGETS_DECLARE_PROPERTY_D_NOTIFY(NavigationButtonTrigger, navigationButtonTrigger, navigationButtonTrigger, setNavigationButtonTrigger, navigationButtonTriggerChanged)

    explicit ExCarousel(QWidget *parent = nullptr);
    ~ExCarousel() override;

    int addSlide(QWidget *widget);
    int addPixmap(const QPixmap &pixmap);
    int addPixmap(const QPixmap &pixmap, const QString &title, const QString &subtitle = QString(),
                  Qt::AspectRatioMode aspectMode = Qt::KeepAspectRatioByExpanding);
    int addImage(const QString &filePath, const QString &title = QString(), const QString &subtitle = QString(),
                 Qt::AspectRatioMode aspectMode = Qt::KeepAspectRatioByExpanding);
    void insertSlide(int index, QWidget *widget);
    void removeSlide(int index);
    QWidget *slide(int index) const;
    QWidget *takeSlide(int index);

    int count() const;

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

public Q_SLOTS:
    void next();
    void previous();

Q_SIGNALS:
    void slideClicked(int index);


protected:
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    void enterEvent(QEnterEvent *event) override;
#else
    void enterEvent(QEvent *event) override;
#endif
    void leaveEvent(QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void changeEvent(QEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    QScopedPointer<ExCarouselPrivate> d;
    friend class ExCarouselPrivate;
};
