#ifndef FILE_MDHTML_MD_FONT_METRICS_H_
#define FILE_MDHTML_MD_FONT_METRICS_H_

#include <string>
#include <string_view>

struct MdFontSpec {
  std::string family;
  double size_px = 16.0;
  int weight = 400;
  bool italic = false;
};

struct MdTextMetrics {
  double width = 0.0;
  double ascent = 0.0;
  double descent = 0.0;
  int line_count = 1;
};

// Host-injected text measurement. Implementations must be const and
// thread-safe: several workers may call them at the same time. The default
// implementation is deterministic and does not touch system fonts; hosts
// that render real text (the Qt adapter) inject their own so Mermaid node
// sizes match the rasterizer.
class MdFontMetrics {
 public:
  virtual ~MdFontMetrics() = default;

  // Picks a concrete family from a CSS font-family list. Returns the input
  // unchanged when nothing better is known.
  virtual std::string ResolveFamily(std::string_view css_list) const = 0;

  // Measures a single line; line_count is always 1 here. Wrapping is the
  // caller's job using repeated Measure calls.
  virtual MdTextMetrics Measure(std::string_view text,
                                const MdFontSpec& font) const = 0;

  // Stable identity of the metric source, folded into render/cache keys so
  // a font change invalidates cached diagrams.
  virtual std::string Fingerprint() const { return "metrics-v1"; }
};

// Deterministic approximation for hosts that cannot provide real metrics.
// Latin advances come from a small category table, CJK/fullwidth/emoji count
// as 1em, combining marks as 0.
class MdDefaultFontMetrics final : public MdFontMetrics {
 public:
  std::string ResolveFamily(std::string_view css_list) const override;
  MdTextMetrics Measure(std::string_view text,
                        const MdFontSpec& font) const override;
};

#endif  // FILE_MDHTML_MD_FONT_METRICS_H_
