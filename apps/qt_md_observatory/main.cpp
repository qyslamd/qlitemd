#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QLocale>
#include <QSettings>
#include <QTimer>
#include <QTranslator>

#include "app_theme.h"
#include "observatory_window.h"

int main(int argc, char* argv[]) {
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
  QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
  QCoreApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
#endif
  QApplication app(argc, argv);
  QCoreApplication::setOrganizationName(QStringLiteral("qlitemd"));
  QCoreApplication::setApplicationName(QStringLiteral("qt_md_observatory"));

  QTranslator translator;
  const QString qm = QCoreApplication::applicationDirPath() +
                     QStringLiteral("/translations/qt_md_observatory_zh_CN.qm");
  if (QLocale::system().language() == QLocale::Chinese && translator.load(qm)) {
    app.installTranslator(&translator);
  }

  // No FluentUIStyle here: Fusion gives a predictable base for the app sheet.
  app.setStyle(QStringLiteral("Fusion"));

  // Seed the application palette before any widget exists. QWindowKit creates
  // its non-client title-bar host during show; creating it under the right
  // palette avoids a stale dark chrome in the light theme.
  const bool dark =
      QSettings().value(QStringLiteral("appearance/dark"), true).toBool();
  app.setPalette(AppPalette(dark));
  app.setStyleSheet(AppStyleSheet(dark));

  QString root = QDir::currentPath();
  if (argc > 1) {
    const QDir candidate(QString::fromLocal8Bit(argv[1]));
    if (candidate.exists()) {
      root = candidate.absolutePath();
    }
  }

  ObservatoryWindow window;
  window.show();
  QTimer::singleShot(0, &window,
                     [&window, root] { window.OpenWorkspace(root); });
  return app.exec();
}
