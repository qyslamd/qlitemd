// qt_md_viewer 入口：观感旋钮照 FluentUIStyle 官方 Gallery 抄。
// 固定 fluent——不加载任何 QSS（样式表一生效就会绕过 Fluent 绘制）
#include <fluentui3style.h>

#include <QApplication>
#include <QFileInfo>
#include <QFont>
#include <QFontDatabase>
#include <QIcon>
#include <QLocale>
#include <QStringList>
#include <QTranslator>

#include "md_viewer_settings.h"
#include "md_viewer_window.h"

namespace {

// argv[0] 是可执行文件；当前没有命令行选项，取第一个非选项位置参数。
// 每个进程只打开自己的参数，不再向已有实例转发
QString FirstPathArgument(const QStringList& arguments) {
  for (int i = 1; i < arguments.size(); ++i) {
    const QString& value = arguments.at(i);
    if (!value.isEmpty() && !value.startsWith(QLatin1Char('-'))) {
      return QFileInfo(value).absoluteFilePath();
    }
  }
  return {};
}

// 界面字体随包分发；文件缺失时退回系统字体，不让界面变成方框
QString UiFontFamily() {
  const QString dir =
      QCoreApplication::applicationDirPath() + QStringLiteral("/fonts");
  bool loaded = false;
  for (const QString& name : {QStringLiteral("MiSans-Regular.ttf"),
                              QStringLiteral("MiSans-Demibold.ttf")}) {
    if (QFontDatabase::addApplicationFont(dir + QLatin1Char('/') + name) >= 0) {
      loaded = true;
    }
  }
  return loaded ? QStringLiteral("MiSans") : QStringLiteral("Microsoft YaHei");
}

}  // namespace

int main(int argc, char** argv) {
  QApplication app(argc, argv);
  QCoreApplication::setApplicationName(QStringLiteral("qt_md_viewer"));

  // 设置在样式创建前读：主题与模糊决定 Fluent 的初始调色板
  MdViewerSettings settings;
  settings.Load();

  // 图标走 qrc（windeployqt 会带上 qsvg 插件——markdown 模块链了 Qt5::Svg）
  app.setWindowIcon(QIcon(QStringLiteral(":/resources/svg/icon.svg")));

  // 官方 Gallery 的同款旋钮：二级圆角与配色方案（0=Fluent，1=Teams）
  app.setProperty("secondLevelRoundingRadius", 3);
  app.setProperty("_q_themestyle", 0);
  app.setProperty("_q_colorscheme", settings.dark_theme ? 1 : 0);
#ifdef Q_OS_WIN
  // 2 对应 Gallery 的 DwmBlur：调色板给 Base/Window 留出透明度，
  // DWM 模糊才有地方透出来
  app.setProperty("_q_widget_mode",
                  settings.window_material == WindowMaterial::kNone ? 0 : 2);
#endif
  app.setStyle(new FluentUI3Style);
  app.setPalette(app.style()->standardPalette());

  QFont app_font = app.font();
  app_font.setFamily(UiFontFamily());
  app.setFont(app_font);

  // 界面源语言是英文；中文系统加载 zh_CN 翻译，其余语言直接用源文。
  // qm 与字体一样放在 exe 旁的 translations/ 目录（见 CMake i18n 目标）
  static QTranslator translator;
  if (QLocale::system().language() == QLocale::Chinese) {
    const QString dir = QCoreApplication::applicationDirPath() +
                        QStringLiteral("/translations");
    if (translator.load(QStringLiteral("qt_md_viewer_zh_CN"), dir)) {
      app.installTranslator(&translator);
    }
  }

  MdViewerWindow window(&settings);
  // 恢复要在 show 前完成：树、文档、滚动位置一次性就位
  const QString path = FirstPathArgument(app.arguments());
  if (!path.isEmpty()) {
    window.OpenPath(path);
  } else {
    window.RestoreSession();
  }
  window.show();
  return app.exec();
}
