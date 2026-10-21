#include <excolorpicker.h>
#include <excolorpickerdialog.h>

#include <QLabel>
#include <QLineEdit>
#include <QPointer>
#include <QPushButton>
#include <QSignalSpy>
#include <QTabBar>
#include <QTimer>
#include <QtTest>

class ColorPickerDialogTests : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void confirmAndCancelAcrossReopens();
    void escapeFromColorInput();
    void enterCommitsPendingHexInput();
    void closeRestoresOpeningColor();
    void footerFillsDialog_data();
    void footerFillsDialog();
    void isStandardFramedDialogWithoutOverlay();
    void enterOnCancelRejects();
    void selectionSignalSeesFinishedDialog();
    void closeCallbacksDoNotSubmitTwice();
    void settingSameColorDoesNotNotify();
    void rejectionCallbackDoesNotCloseTwice();
    void acceptedHandlerMayDeleteDialog();
    void colorFeedbackDoesNotRepeat();
};

void ColorPickerDialogTests::confirmAndCancelAcrossReopens()
{
    ExColorPickerDialog dialog;
    dialog.setColor(Qt::blue);
    QSignalSpy selected(&dialog, &ExColorPickerDialog::colorSelected);
    QSignalSpy accepted(&dialog, &QDialog::accepted);
    QSignalSpy rejected(&dialog, &QDialog::rejected);
    dialog.open();
    dialog.colorPicker()->setColor(Qt::red);
    QCOMPARE(selected.count(), 0);
    auto *ok = dialog.findChild<QPushButton *>(QStringLiteral("exColorPickerDialogAccept"));
    auto *cancel = dialog.findChild<QPushButton *>(QStringLiteral("exColorPickerDialogCancel"));
    QVERIFY(ok);
    QVERIFY(cancel);
    ok->click();
    QCOMPARE(selected.count(), 1);
    QCOMPARE(qvariant_cast<QColor>(selected.at(0).at(0)), QColor(Qt::red));
    QCOMPARE(accepted.count(), 1);
    QVERIFY(!dialog.isVisible());

    dialog.open();
    QCOMPARE(dialog.color(), QColor(Qt::red));
    dialog.colorPicker()->setColor(Qt::green);
    cancel->click();
    QCOMPARE(dialog.color(), QColor(Qt::red));
    QCOMPARE(selected.count(), 1);
    QCOMPARE(rejected.count(), 1);
    dialog.open();
    QCOMPARE(dialog.color(), QColor(Qt::red));
    QVERIFY(dialog.colorPicker()->isVisible());
    dialog.reject();
}

void ColorPickerDialogTests::escapeFromColorInput()
{
    ExColorPickerDialog dialog;
    dialog.setColor(Qt::blue);
    QSignalSpy selected(&dialog, &ExColorPickerDialog::colorSelected);
    QSignalSpy rejected(&dialog, &QDialog::rejected);
    dialog.open();
    auto *tabs = dialog.colorPicker()->findChild<QTabBar *>();
    auto *hex = dialog.colorPicker()->findChild<QLineEdit *>();
    QVERIFY(tabs);
    QVERIFY(hex);
    tabs->setCurrentIndex(2);
    QTRY_VERIFY(hex->isVisible());
    dialog.colorPicker()->setColor(Qt::red);
    hex->setFocus();
    QTest::keyClick(hex, Qt::Key_Escape);
    QCOMPARE(rejected.count(), 1);
    QCOMPARE(selected.count(), 0);
    QCOMPARE(dialog.color(), QColor(Qt::blue));
    QVERIFY(!dialog.isVisible());
}

void ColorPickerDialogTests::enterCommitsPendingHexInput()
{
    ExColorPickerDialog dialog;
    QSignalSpy selected(&dialog, &ExColorPickerDialog::colorSelected);
    dialog.open();
    auto *tabs = dialog.colorPicker()->findChild<QTabBar *>();
    auto *hex = dialog.colorPicker()->findChild<QLineEdit *>();
    QVERIFY(tabs);
    QVERIFY(hex);
    tabs->setCurrentIndex(2);
    QTRY_VERIFY(hex->isVisible());
    hex->setFocus();
    hex->selectAll();
    QTest::keyClicks(hex, "12AB34");
    QTest::keyClick(hex, Qt::Key_Return);
    QCOMPARE(selected.count(), 1);
    QCOMPARE(qvariant_cast<QColor>(selected.at(0).at(0)), QColor(QStringLiteral("#12AB34")));
    QCOMPARE(dialog.result(), int(QDialog::Accepted));
    QVERIFY(!dialog.isVisible());
}

void ColorPickerDialogTests::closeRestoresOpeningColor()
{
    ExColorPickerDialog dialog;
    const QColor initial(20, 40, 60, 100);
    dialog.colorPicker()->setAlphaEnabled(true);
    dialog.setColor(initial);
    QSignalSpy selected(&dialog, &ExColorPickerDialog::colorSelected);
    dialog.open();
    dialog.setColor(Qt::yellow);
    dialog.close();
    QCOMPARE(dialog.color(), initial);
    QCOMPARE(dialog.result(), int(QDialog::Rejected));
    QCOMPARE(selected.count(), 0);
}

void ColorPickerDialogTests::footerFillsDialog_data()
{
    QTest::addColumn<bool>("dark");
    QTest::newRow("light") << false;
    QTest::newRow("dark") << true;
}

void ColorPickerDialogTests::footerFillsDialog()
{
    QFETCH(bool, dark);
    ExColorPickerDialog dialog;
    QPalette palette = dialog.palette();
    palette.setColor(QPalette::Window, dark ? QColor(32, 32, 32) : QColor(243, 243, 243));
    palette.setColor(QPalette::Base, dark ? QColor(39, 39, 39) : QColor(249, 249, 249));
    palette.setColor(QPalette::WindowText, dark ? Qt::white : Qt::black);
    dialog.setPalette(palette);
    dialog.setTitle(QStringLiteral("编辑颜色"));
    dialog.open();
    auto *title = dialog.findChild<QLabel *>(QStringLiteral("exColorPickerDialogTitle"));
    auto *ok = dialog.findChild<QPushButton *>(QStringLiteral("exColorPickerDialogAccept"));
    auto *cancel = dialog.findChild<QPushButton *>(QStringLiteral("exColorPickerDialogCancel"));
    QVERIFY(title);
    QVERIFY(ok);
    QVERIFY(cancel);
    QCOMPARE(title->text(), dialog.title());
    for (const int width : {480, 720})
    {
        dialog.resize(width, dialog.sizeHint().height());
        QCoreApplication::processEvents();
        QVERIFY(qAbs(ok->width() - cancel->width()) <= 1);
        QVERIFY(ok->height() >= 40);
        QVERIFY(cancel->height() >= 40);
        QVERIFY(ok->x() < cancel->x());
        QCOMPARE(ok->x(), ok->parentWidget()->width() - cancel->geometry().right() - 1);
        QVERIFY(ok->width() + cancel->width() >= dialog.width() - 80);
        QVERIFY(title->geometry().bottom() < dialog.colorPicker()->y());
        QVERIFY(dialog.colorPicker()->geometry().bottom() < ok->parentWidget()->y());
    }
}

void ColorPickerDialogTests::isStandardFramedDialogWithoutOverlay()
{
    QWidget host;
    host.resize(900, 700);
    host.show();
    ExColorPickerDialog dialog(&host);
    QVERIFY(!(dialog.windowFlags() & Qt::FramelessWindowHint));
    QVERIFY(!dialog.testAttribute(Qt::WA_TranslucentBackground));
    dialog.open();
    QVERIFY(dialog.isVisible());
    QVERIFY(!host.findChild<QWidget *>(QStringLiteral("exColorPickerDialogOverlay")));
    QVERIFY(!dialog.findChild<QWidget *>(QStringLiteral("exColorPickerDialogOverlay")));
    dialog.reject();
    QTimer::singleShot(0, &dialog, [&]() { dialog.reject(); });
    QCOMPARE(dialog.exec(), int(QDialog::Rejected));
    QVERIFY(!host.findChild<QWidget *>(QStringLiteral("exColorPickerDialogOverlay")));
}

void ColorPickerDialogTests::enterOnCancelRejects()
{
    ExColorPickerDialog dialog;
    dialog.setColor(Qt::blue);
    dialog.open();
    dialog.setColor(Qt::red);
    auto *cancel = dialog.findChild<QPushButton *>(QStringLiteral("exColorPickerDialogCancel"));
    QVERIFY(cancel);
    cancel->setFocus();
    QSignalSpy selected(&dialog, &ExColorPickerDialog::colorSelected);
    QSignalSpy rejected(&dialog, &QDialog::rejected);
    QTest::keyClick(cancel, Qt::Key_Return);
    QCOMPARE(rejected.count(), 1);
    QCOMPARE(selected.count(), 0);
    QCOMPARE(dialog.color(), QColor(Qt::blue));
}

void ColorPickerDialogTests::selectionSignalSeesFinishedDialog()
{
    QWidget host;
    host.show();
    ExColorPickerDialog dialog(&host);
    bool visibleAtSelection = true;
    int resultAtSelection = QDialog::Rejected;
    connect(&dialog, &ExColorPickerDialog::colorSelected, &dialog, [&]()
    {
        visibleAtSelection = dialog.isVisible();
        resultAtSelection = dialog.result();
    });
    dialog.open();
    dialog.accept();
    QVERIFY(!visibleAtSelection);
    QCOMPARE(resultAtSelection, int(QDialog::Accepted));
}

void ColorPickerDialogTests::closeCallbacksDoNotSubmitTwice()
{
    ExColorPickerDialog dialog;
    QSignalSpy selected(&dialog, &ExColorPickerDialog::colorSelected);
    QSignalSpy accepted(&dialog, &QDialog::accepted);
    // Bound the callback so a regression fails rather than overflowing the stack.
    bool reentered = false;
    connect(&dialog, &ExColorPickerDialog::colorSelected, &dialog, [&]()
    {
        if (!reentered)
        {
            reentered = true;
            dialog.accept();
        }
    });
    dialog.open();
    dialog.accept();
    QCOMPARE(selected.count(), 1);
    QCOMPARE(accepted.count(), 1);
}

void ColorPickerDialogTests::settingSameColorDoesNotNotify()
{
    ExColorPickerDialog dialog;
    QSignalSpy changed(&dialog, &ExColorPickerDialog::colorChanged);
    dialog.setColor(dialog.color());
    QCOMPARE(changed.count(), 0);
    dialog.colorPicker()->setAlphaEnabled(false);
    dialog.setColor(Qt::blue);
    changed.clear();
    dialog.setColor(QColor(0, 0, 255, 100));
    QCOMPARE(changed.count(), 0);
}

void ColorPickerDialogTests::rejectionCallbackDoesNotCloseTwice()
{
    ExColorPickerDialog dialog;
    dialog.setColor(Qt::blue);
    dialog.open();
    dialog.setColor(Qt::red);
    QSignalSpy rejected(&dialog, &QDialog::rejected);
    bool reentered = false;
    connect(&dialog, &ExColorPickerDialog::colorChanged, &dialog, [&]()
    {
        if (!reentered)
        {
            reentered = true;
            dialog.reject();
        }
    });
    dialog.reject();
    QCOMPARE(rejected.count(), 1);
    QCOMPARE(dialog.color(), QColor(Qt::blue));
}

void ColorPickerDialogTests::acceptedHandlerMayDeleteDialog()
{
    QWidget host;
    host.show();
    QPointer<ExColorPickerDialog> dialog = new ExColorPickerDialog(&host);
    connect(dialog, &QDialog::accepted, &host, [&]() { delete dialog.data(); });
    dialog->open();
    dialog->accept();
    QVERIFY(!dialog);
}

void ColorPickerDialogTests::colorFeedbackDoesNotRepeat()
{
    ExColorPickerDialog dialog;
    QSignalSpy changed(&dialog, &ExColorPickerDialog::colorChanged);
    connect(&dialog, &ExColorPickerDialog::colorChanged, &dialog, [&](const QColor &color)
    {
        if (changed.count() < 3)
            dialog.setColor(color);
    });
    dialog.setColor(Qt::red);
    QCOMPARE(changed.count(), 1);
}

QTEST_MAIN(ColorPickerDialogTests)
#include "tst_colorpickerdialog.moc"
