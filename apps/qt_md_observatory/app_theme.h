#ifndef FILE_APPS_QT_MD_OBSERVATORY_APP_THEME_H_
#define FILE_APPS_QT_MD_OBSERVATORY_APP_THEME_H_

#include <QPalette>
#include <QString>

// The app deliberately does not use FluentUIStyle: the palette and sheet are
// the whole styling layer, so a theme is just two values here.
QString AppStyleSheet(bool dark);
QPalette AppPalette(bool dark);

#endif  // FILE_APPS_QT_MD_OBSERVATORY_APP_THEME_H_
