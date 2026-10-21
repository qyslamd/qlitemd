// Markdown 查看器的入口：观感旋钮照 FluentUIStyle 官方 Gallery 抄。
// 本示例固定 fluent——不加载任何 QSS（样式表一生效就会绕过 Fluent 绘制）
#include <fluentui3style.h>

#include <QApplication>
#include <QFileInfo>
#include <QFont>
#include <QIcon>
#include <QStringList>

#include "common/single_instance.h"
#include "md_viewer_settings.h"
#include "md_viewer_window.h"

namespace {

// argv[0] 是可执行文件；当前没有命令行选项，取第一个非选项位置参数。
// 第二实例必须按自己的当前目录把相对路径转成绝对路径再转发
QString FirstPathArgument(const QStringList& arguments) {
  for (int i = 1; i < arguments.size(); ++i) {
    const QString& value = arguments.at(i);
    if (!value.isEmpty() && !value.startsWith(QLatin1Char('-'))) {
      return QFileInfo(value).absoluteFilePath();
    }
  }
  return {};
}

}  // namespace

int main(int argc, char** argv) {
  QApplication app(argc, argv);
  QCoreApplication::setApplicationName(QStringLiteral("md_viewer"));

  // 单实例先于设置与窗口：第二实例只负责转发参数，然后立即退出
  SingleInstance single_instance(QCoreApplication::applicationName(),
                                 QStringLiteral("md_viewerMutex"));
  if (single_instance.isAppAlreadyRunning()) {
    single_instance.connectToExistingInstance(QCoreApplication::arguments());
    return 0;
  }
  if (!single_instance.startListening() &&
      single_instance.isAppAlreadyRunning()) {
    // 探测与监听之间被另一个进程抢先：仍然按第二实例转发，避免起第二个 UI
    single_instance.connectToExistingInstance(QCoreApplication::arguments());
    return 0;
  }

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
  app.setProperty("_q_widget_mode", settings.window_blur ? 2 : 0);
#endif
  app.setStyle(new FluentUI3Style);

  QFont app_font = app.font();
  app_font.setFamily(QStringLiteral("Microsoft YaHei"));
  app.setFont(app_font);

  MdViewerWindow window(&settings);
  single_instance.setActivationWindow(reinterpret_cast<void*>(window.winId()));
  QObject::connect(&single_instance, &SingleInstance::newInstanceLaunched,
                   &window, [&window](const QStringList& arguments) {
                     const QString path = FirstPathArgument(arguments);
                     if (!path.isEmpty()) {
                       window.OpenPath(path);
                     }
                   });
  QObject::connect(&single_instance, &SingleInstance::showApplication, &window,
                   [&window] {
                     window.showNormal();
                     window.raise();
                     window.activateWindow();
                   });
  window.show();

  // 第一个位置参数：目录就铺开侧栏，文件直接看；没有参数就接着看上次的
  const QString path = FirstPathArgument(app.arguments());
  if (!path.isEmpty()) {
    window.OpenPath(path);
  } else {
    window.OpenLastFile();
  }
  return app.exec();
}
