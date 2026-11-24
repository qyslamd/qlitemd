#include "reader_preferences.h"

#include <QStringList>

QString ReaderAppearanceCss(const ReaderPreferences& preferences,
                            bool prototype_layout) {
  QStringList rules;
  if (preferences.font_size > 0 || preferences.line_height_percent > 0) {
    QString body = QStringLiteral("body {");
    if (preferences.font_size > 0) {
      body += QStringLiteral(" font-size: %1px;")
                  .arg(qBound(10, preferences.font_size, 24));
    }
    if (preferences.line_height_percent > 0) {
      const qreal line_height =
          qBound(120, preferences.line_height_percent, 240) / 100.0;
      body += QStringLiteral(" line-height: %1;").arg(line_height, 0, 'f', 2);
    }
    body += QStringLiteral(" }");
    rules.append(body);
  }
  if (preferences.measure_px >= 0) {
    const QString width = preferences.measure_px == 0
                              ? QStringLiteral("none")
                              : QStringLiteral("%1px").arg(
                                    qBound(480, preferences.measure_px, 1600));
    if (prototype_layout) {
      rules.append(
          QStringLiteral(
              "h1, h2, h3, h4, h5, h6, p, ul, ol, blockquote, "
              ".admonition-note, .admonition-tip, .admonition-important, "
              ".admonition-warning, .admonition-caution, .callout, "
              "section.footnotes { max-width: %1; }")
              .arg(width));
    } else {
      rules.append(QStringLiteral(".md-page { max-width: %1; }").arg(width));
    }
  }
  return rules.join(QLatin1Char('\n'));
}
