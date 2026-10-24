#include "md_font_metrics.h"

#include <string>

#include <gtest/gtest.h>

namespace {

TEST(MdFontMetricsTest, ResolveFamilyUnquotesFirstEntry) {
  const MdDefaultFontMetrics metrics;
  EXPECT_EQ(metrics.ResolveFamily("\"Segoe UI\", sans-serif"), "Segoe UI");
  EXPECT_EQ(metrics.ResolveFamily("'Noto Sans CJK SC', serif"),
            "Noto Sans CJK SC");
  EXPECT_EQ(metrics.ResolveFamily("sans-serif"), "sans-serif");
  EXPECT_EQ(metrics.ResolveFamily(""), "sans-serif");
}

TEST(MdFontMetricsTest, EmptyTextHasNoWidth) {
  const MdDefaultFontMetrics metrics;
  const MdTextMetrics size =
      metrics.Measure("", MdFontSpec{std::string(), 16.0, 400, false});
  EXPECT_DOUBLE_EQ(size.width, 0.0);
  EXPECT_DOUBLE_EQ(size.ascent, 0.0);
}

TEST(MdFontMetricsTest, CjkIsWiderThanLatin) {
  const MdDefaultFontMetrics metrics;
  const MdFontSpec spec{std::string(), 16.0, 400, false};
  const double cjk = metrics.Measure("\xE4\xB8\xAD\xE6\x96\x87", spec).width;
  const double latin = metrics.Measure("ab", spec).width;
  EXPECT_GT(cjk, latin);
  EXPECT_DOUBLE_EQ(cjk, 32.0);
}

TEST(MdFontMetricsTest, AscentAndDescentAreDeterministic) {
  const MdDefaultFontMetrics metrics;
  const MdTextMetrics size =
      metrics.Measure("x", MdFontSpec{std::string(), 20.0, 400, false});
  EXPECT_DOUBLE_EQ(size.ascent, 16.0);
  EXPECT_DOUBLE_EQ(size.descent, 4.0);
  EXPECT_EQ(size.line_count, 1);
}

TEST(MdFontMetricsTest, BoldDoesNotChangeDefaultWidth) {
  const MdDefaultFontMetrics metrics;
  const double normal =
      metrics.Measure("word", MdFontSpec{std::string(), 16.0, 400, false})
          .width;
  const double bold =
      metrics.Measure("word", MdFontSpec{std::string(), 16.0, 700, false})
          .width;
  EXPECT_DOUBLE_EQ(normal, bold);
}

TEST(MdFontMetricsTest, FingerprintIsStable) {
  const MdDefaultFontMetrics metrics;
  EXPECT_EQ(metrics.Fingerprint(), metrics.Fingerprint());
  EXPECT_FALSE(metrics.Fingerprint().empty());
}

}  // namespace
