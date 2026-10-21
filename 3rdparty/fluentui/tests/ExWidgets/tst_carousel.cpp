#include <excarousel.h>

#include <QLabel>
#include <QSignalSpy>
#include <QtTest>

class CarouselTests : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void addAndNavigate();
    void wrapAround();
    void takeSlideUpdatesIndex();
    void imageSlideAndProperties();
};

void CarouselTests::addAndNavigate()
{
    ExCarousel carousel;
    QCOMPARE(carousel.count(), 0);
    QCOMPARE(carousel.currentIndex(), -1);

    auto *a = new QLabel(QStringLiteral("A"));
    auto *b = new QLabel(QStringLiteral("B"));
    auto *c = new QLabel(QStringLiteral("C"));
    QCOMPARE(carousel.addSlide(a), 0);
    QCOMPARE(carousel.addSlide(b), 1);
    QCOMPARE(carousel.addSlide(c), 2);
    QCOMPARE(carousel.count(), 3);
    QCOMPARE(carousel.currentIndex(), 0);
    QCOMPARE(carousel.slide(1), b);

    carousel.setAnimationDuration(0);
    QSignalSpy spy(&carousel, &ExCarousel::currentIndexChanged);
    carousel.setCurrentIndex(2);
    QCOMPARE(carousel.currentIndex(), 2);
    QCOMPARE(spy.count(), 1);
}

void CarouselTests::wrapAround()
{
    ExCarousel carousel;
    carousel.setAnimationDuration(0);
    carousel.setWrap(true);
    carousel.addSlide(new QLabel(QStringLiteral("A")));
    carousel.addSlide(new QLabel(QStringLiteral("B")));
    carousel.addSlide(new QLabel(QStringLiteral("C")));

    carousel.next();
    QCOMPARE(carousel.currentIndex(), 1);
    carousel.next();
    QCOMPARE(carousel.currentIndex(), 2);
    carousel.next();
    QCOMPARE(carousel.currentIndex(), 0);
    carousel.previous();
    QCOMPARE(carousel.currentIndex(), 2);
}

void CarouselTests::takeSlideUpdatesIndex()
{
    ExCarousel carousel;
    carousel.setAnimationDuration(0);
    carousel.addSlide(new QLabel(QStringLiteral("A")));
    carousel.addSlide(new QLabel(QStringLiteral("B")));
    carousel.addSlide(new QLabel(QStringLiteral("C")));
    carousel.setCurrentIndex(2);

    QWidget *taken = carousel.takeSlide(0);
    QVERIFY(taken);
    QCOMPARE(taken->parent(), nullptr);
    QCOMPARE(carousel.count(), 2);
    QCOMPARE(carousel.currentIndex(), 1);
    delete taken;
}

void CarouselTests::imageSlideAndProperties()
{
    ExCarousel carousel;
    carousel.setAnimationDuration(0);

    QPixmap pix(100, 100);
    pix.fill(Qt::blue);
    int idx1 = carousel.addPixmap(pix, QStringLiteral("Title 1"), QStringLiteral("Subtitle 1"));
    QCOMPARE(idx1, 0);
    QCOMPARE(carousel.count(), 1);

    carousel.setBorderRadius(12.0);
    QCOMPARE(carousel.borderRadius(), 12.0);

    carousel.setNavigationButtonTrigger(ExCarousel::OnHover);
    QCOMPARE(carousel.navigationButtonTrigger(), ExCarousel::OnHover);
}

QTEST_MAIN(CarouselTests)
#include "tst_carousel.moc"
