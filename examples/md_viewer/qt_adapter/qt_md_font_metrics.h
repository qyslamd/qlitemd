#ifndef FILE_MD_VIEWER_QT_MD_FONT_METRICS_H_
#define FILE_MD_VIEWER_QT_MD_FONT_METRICS_H_

#include <QFontDatabase>
#include <QString>

#include "md_font_metrics.h"

// Qt-backed metrics for the mdhtml core. Mirrors the measurement the old
// Mermaid renderer used: a CJK-capable family is picked on the GUI thread,
// QFontMetricsF runs on whichever worker calls Measure, and the fingerprint
// matches FontFingerprint() so persistent cache keys stay stable.
class QtMdFontMetrics final : public MdFontMetrics {
 public:
  // Must be constructed after QGuiApplication exists (GUI thread).
  QtMdFontMetrics();

  std::string ResolveFamily(std::string_view css_list) const override;
  MdTextMetrics Measure(std::string_view text,
                        const MdFontSpec& font) const override;
  std::string Fingerprint() const override;

  // Family used when rasterizing SVG (empty = leave the SVG's own family).
  QString family() const { return cjk_family_; }

 private:
  QString cjk_family_;
  QFontDatabase database_;
  QString fingerprint_;
};

#endif  // FILE_MD_VIEWER_QT_MD_FONT_METRICS_H_
