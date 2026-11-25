#ifndef FILE_TESTS_SUPPORT_TEST_I18N_HELPER_H_
#define FILE_TESTS_SUPPORT_TEST_I18N_HELPER_H_

#include <QCoreApplication>
#include <QString>
#include <QTranslator>

// 断言中文界面文本的测试用它加载 zh_CN qm。与 main.cpp 的加载逻辑一致，
// 但无条件安装：翻译断言在英文 locale 的 CI 上也要成立。qm 在
// build/bin/translations，测试二进制在 build/bin/tests，用相对路径回溯。
inline void InstallTestTranslator() {
  static QTranslator translator;
  const QString dir = QCoreApplication::applicationDirPath() +
                      QStringLiteral("/../translations");
  if (translator.load(QStringLiteral("qt_md_viewer_zh_CN"), dir)) {
    QCoreApplication::installTranslator(&translator);
  }
}

#endif  // FILE_TESTS_SUPPORT_TEST_I18N_HELPER_H_
