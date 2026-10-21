#include <QApplication>
#include <QFont>
#include <QGuiApplication>
#include "mainwindow.h"

#ifdef HAS_FLUENTUI3_STYLE_LIB
#include "fluentui3style.h"
#endif

int main(int argc, char* argv[])
{
#if (QT_VERSION < QT_VERSION_CHECK(6, 0, 0))
    QGuiApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QGuiApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
    QGuiApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
#else
    //改成屏幕实际缩放的
    qputenv("QT_SCREEN_SCALE_FACTORS", "1.25");
#endif
#endif

    QApplication application(argc, argv);
    QApplication::addLibraryPath(QCoreApplication::applicationDirPath() + "/../plugins");
    
    application.setApplicationName(QStringLiteral("Win11 Clock"));
    application.setOrganizationName(QStringLiteral("Window11Style"));

    QFont font(QStringLiteral("Microsoft YaHei UI"));
    font.setPixelSize(13);
    font.setHintingPreference(QFont::PreferNoHinting);
    application.setFont(font);

    application.setProperty("_q_scrollHint_center", false); //控制QComboBox弹出位置，默认false，true则在QComboBox中心位置弹出
    application.setProperty("_q_colorscheme", 1);
    application.setProperty("_q_themestyle", 0);
#ifdef Q_OS_WIN
    application.setStyle(QStringLiteral("FluentUI3"));
#elif defined(HAS_FLUENTUI3_STYLE_LIB)
    application.setStyle(new FluentUI3Style);
#endif

    MainWindow window;
    window.show();

    return application.exec();
}
