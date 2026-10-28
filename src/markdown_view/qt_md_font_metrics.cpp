#include "qt_md_font_metrics.h"

#include <algorithm>
#include <cmath>

#include <QFont>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QStringList>

namespace {

QString PickCjkFamily() {
  if (qGuiApp == nullptr) {
    return {};
  }
  const QStringList candidates = {
      QStringLiteral("Microsoft YaHei"),     // Windows
      QStringLiteral("Noto Sans CJK SC"),    // Linux
      QStringLiteral("Source Han Sans SC"),  // 思源黑体
      QStringLiteral("WenQuanYi Micro Hei"), QStringLiteral("SimHei"),
  };
  const QFontDatabase database;
  const QStringList families = database.families();
  for (const QString& name : candidates) {
    if (families.contains(name)) {
      return name;
    }
  }
  return {};
}

}  // namespace

QtMdFontMetrics::QtMdFontMetrics()
    : cjk_family_(PickCjkFamily()), database_(QFontDatabase()) {
  const QString family =
      cjk_family_.isEmpty() ? QStringLiteral("sans-serif") : cjk_family_;
  QFont font(family);
  font.setPixelSize(16);
  const QFontMetricsF metrics(font);
  fingerprint_ =
      QStringLiteral("%1:%2:%3:%4")
          .arg(family)
          .arg(qRound(metrics.ascent() * 100.0))
          .arg(qRound(metrics.descent() * 100.0))
          .arg(
              qRound(metrics.horizontalAdvance(QStringLiteral("中A")) * 100.0));
}

std::string QtMdFontMetrics::ResolveFamily(std::string_view css_list) const {
  QStringList families =
      QString::fromUtf8(css_list.data(), static_cast<int>(css_list.size()))
          .split(QLatin1Char(','));
  for (QString family : families) {
    family = family.trimmed();
    while (family.endsWith(QLatin1Char(';'))) {
      family.chop(1);
      family = family.trimmed();
    }
    if (family.size() >= 2 && ((family.startsWith(QLatin1Char('"')) &&
                                family.endsWith(QLatin1Char('"'))) ||
                               (family.startsWith(QLatin1Char('\'')) &&
                                family.endsWith(QLatin1Char('\''))))) {
      family = family.mid(1, family.size() - 2).trimmed();
    }
    if (family.compare(QLatin1String("sans-serif"), Qt::CaseInsensitive) == 0) {
      family = cjk_family_.isEmpty() ? QStringLiteral("Arial") : cjk_family_;
    } else if (family.compare(QLatin1String("serif"), Qt::CaseInsensitive) ==
               0) {
      family = QStringLiteral("Times New Roman");
    } else if (family.compare(QLatin1String("monospace"),
                              Qt::CaseInsensitive) == 0) {
      family = QStringLiteral("Consolas");
    }
    if (!family.isEmpty() && database_.hasFamily(family)) {
      return family.toStdString();
    }
  }
  return cjk_family_.isEmpty() ? "Arial" : cjk_family_.toStdString();
}

MdTextMetrics QtMdFontMetrics::Measure(std::string_view text,
                                       const MdFontSpec& font) const {
  MdTextMetrics metrics;
  const QString family =
      font.family.empty() ? cjk_family_ : QString::fromStdString(font.family);
  QFont qfont(family);
  qfont.setPixelSize(std::max(1, static_cast<int>(std::lround(font.size_px))));
  if (font.weight >= 600) {
    qfont.setWeight(QFont::Bold);
  }
  qfont.setItalic(font.italic);
  const QFontMetricsF font_metrics(qfont);
  const QString text_q =
      QString::fromUtf8(text.data(), static_cast<int>(text.size()));
  metrics.width = font_metrics.horizontalAdvance(text_q);
  metrics.ascent = font_metrics.ascent();
  metrics.descent = font_metrics.descent();
  metrics.line_count = 1;
  return metrics;
}

std::string QtMdFontMetrics::Fingerprint() const {
  return fingerprint_.toStdString();
}
