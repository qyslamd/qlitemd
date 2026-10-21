#include <QApplication>
#include <QTimer>

#include "markdown_view.h"

int main(int argc, char** argv) {
  QApplication app(argc, argv);

  MarkdownView view;
  view.SetContent(QStringLiteral("# qlitemd\n\nMinimal MarkdownView example."),
                  QString());
  view.resize(800, 600);
  view.show();

  QTimer::singleShot(0, &app, &QCoreApplication::quit);
  return app.exec();
}
