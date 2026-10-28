#include <QApplication>
#include <QCommandLineParser>
#include <QTimer>

#include "markdown_view.h"

namespace {

constexpr char kSample[] =
    "# qlitemd MarkdownView\n"
    "\n"
    "A QWidget component from the qlitemd source tree.\n"
    "\n"
    "> [!note] MPE callout\n"
    "> The widget can be embedded in any Qt application.\n"
    "\n"
    "```cpp\n"
    "auto* view = new MarkdownView(parent);\n"
    "view->LoadFile(path);\n"
    "```\n";

}  // namespace

int main(int argc, char** argv) {
  QApplication app(argc, argv);
  QCoreApplication::setApplicationName(
      QStringLiteral("qlitemd_qt_widget_minimal"));

  QCommandLineParser parser;
  parser.addHelpOption();
  QCommandLineOption screenshot_option(
      QStringLiteral("screenshot"),
      QStringLiteral("Save a PNG after the first render and exit."),
      QStringLiteral("path"));
  parser.addOption(screenshot_option);
  QCommandLineOption smoke_option(
      QStringLiteral("smoke"),
      QStringLiteral(
          "Exit after the first render, without taking a screenshot."));
  parser.addOption(smoke_option);
  QCommandLineOption markdown_option(
      QStringLiteral("markdown"),
      QStringLiteral(
          "Markdown file to display (defaults to a built-in sample)."),
      QStringLiteral("path"));
  parser.addOption(markdown_option);
  parser.process(app);

  MarkdownView view;
  view.resize(900, 700);

  const QString screenshot_path = parser.value(screenshot_option);
  const bool smoke = parser.isSet(smoke_option);
  if (!screenshot_path.isEmpty() || smoke) {
    // Connect before triggering the render: a cache hit can commit
    // synchronously and would otherwise race past the signal.
    QObject::connect(
        &view, &MarkdownView::renderFinished, &app,
        [&view, &app, screenshot_path](quint64) {
          QTimer::singleShot(100, &app, [&view, &app, screenshot_path] {
            if (screenshot_path.isEmpty()) {
              app.exit(view.has_document() ? 0 : 1);
              return;
            }
            const bool saved = view.grab().save(screenshot_path);
            app.exit(saved ? 0 : 1);
          });
        });
    QTimer::singleShot(60000, &app, [&app] { app.exit(2); });
  }

  view.show();
  const QString markdown_path = parser.value(markdown_option);
  if (markdown_path.isEmpty()) {
    view.SetContent(QString::fromUtf8(kSample), QString());
  } else {
    view.LoadFile(markdown_path);
  }

  return app.exec();
}
