#ifndef FILE_APPS_QT_MD_VIEWER_THEME_ICONS_H_
#define FILE_APPS_QT_MD_VIEWER_THEME_ICONS_H_

#include <QIcon>
#include <QString>

// 主题相关图标分 svg/light 与 svg/dark 两套（颜色钉在文件里：亮色
// #57606a、暗色 #c9d1d9），切主题时由宿主重新取 QIcon 并刷新控件。
// 语义图标（star-filled / status-dot / icon）不随主题换色，仍在 svg/
// 根目录，直接写资源路径加载。
inline QIcon ThemeIcon(const QString& name, bool dark) {
  // QStringLiteral 只接受字面量，分支先各自成串再拼接
  const QString dir = dark ? QStringLiteral("dark/") : QStringLiteral("light/");
  return QIcon(QStringLiteral(":/resources/svg/") + dir + name +
               QStringLiteral(".svg"));
}

#endif  // FILE_APPS_QT_MD_VIEWER_THEME_ICONS_H_
