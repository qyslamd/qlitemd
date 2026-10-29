#include "md_mermaid.h"

#include <atomic>
#include <memory>
#include <string>

#include <gtest/gtest.h>

#include "md_font_metrics.h"

namespace {

class RecordingMetrics : public MdFontMetrics {
 public:
  std::string ResolveFamily(std::string_view css_list) const override {
    (void)css_list;
    return "Test Sans";
  }

  MdTextMetrics Measure(std::string_view text,
                        const MdFontSpec& font) const override {
    ++calls;
    MdTextMetrics metrics;
    metrics.width = static_cast<double>(text.size()) * font.size_px * 0.5;
    metrics.ascent = font.size_px * 0.8;
    metrics.descent = font.size_px * 0.2;
    return metrics;
  }

  std::string Fingerprint() const override { return "recording-v1"; }

  mutable std::atomic<int> calls{0};
};

#if defined(QLITEMD_MDHTML_MERMAID_MERMAN) || defined(QLITEMD_MDHTML_QUICKJS)

TEST(MdMermaidTest, RendersSvgForBothThemes) {
  auto metrics = std::make_shared<RecordingMetrics>();
  MdMermaidEngine engine(metrics);
  ASSERT_TRUE(engine.EnsureLoaded());

  const MdMermaidResult light =
      engine.Render("graph TD; A[Hello]-->B[World];", MdTheme::Light);
  ASSERT_TRUE(light.ok) << light.error;
  EXPECT_EQ(light.svg.svg.rfind("<svg", 0), 0u) << light.svg.svg;
  EXPECT_GT(light.svg.width, 0);
  EXPECT_GT(light.svg.height, 0);
  EXPECT_GT(metrics->calls.load(), 0);

  const MdMermaidResult dark =
      engine.Render("graph TD; A[Hello]-->B[World];", MdTheme::Dark);
  ASSERT_TRUE(dark.ok) << dark.error;
  EXPECT_GT(dark.svg.width, 0);
  EXPECT_NE(dark.svg.svg, light.svg.svg);
}

TEST(MdMermaidTest, InvalidSourceFails) {
  auto metrics = std::make_shared<RecordingMetrics>();
  MdMermaidEngine engine(metrics);
  ASSERT_TRUE(engine.EnsureLoaded());
  EXPECT_FALSE(engine.Render("this is not mermaid", MdTheme::Light).ok);
}

#else

TEST(MdMermaidTest, MissingBackendFailsGracefully) {
  auto metrics = std::make_shared<RecordingMetrics>();
  MdMermaidEngine engine(metrics);
  EXPECT_FALSE(engine.EnsureLoaded());
  EXPECT_FALSE(engine.Render("graph TD; A-->B;", MdTheme::Light).ok);
}

#endif

TEST(MdMermaidTest, RenderKeyIncludesThemeAndFingerprint) {
  RecordingMetrics metrics;
  const std::string light =
      MdMermaidRenderKey("graph TD; A-->B;", MdTheme::Light, metrics);
  const std::string dark =
      MdMermaidRenderKey("graph TD; A-->B;", MdTheme::Dark, metrics);
  EXPECT_NE(light, dark);
  EXPECT_NE(light.find("recording-v1"), std::string::npos) << light;
  EXPECT_NE(light.find("graph TD; A-->B;"), std::string::npos) << light;
}

TEST(MdMermaidTest, RenderKeySeparatesBackendModes) {
  RecordingMetrics metrics;
  const std::string auto_key = MdMermaidRenderKey(
      "graph TD; A-->B;", MdTheme::Light, metrics, MdMermaidBackend::kAuto);
  const std::string native_key = MdMermaidRenderKey(
      "graph TD; A-->B;", MdTheme::Light, metrics, MdMermaidBackend::kNative);
  const std::string quickjs_key = MdMermaidRenderKey(
      "graph TD; A-->B;", MdTheme::Light, metrics, MdMermaidBackend::kQuickjs);
  EXPECT_NE(auto_key, native_key);
  EXPECT_NE(auto_key, quickjs_key);
  EXPECT_NE(native_key, quickjs_key);
}

#if defined(QLITEMD_MDHTML_QUICKJS)

// A quickjs-explicit request must render through mermaid.js regardless of
// whether merman is compiled in, and must report the backend it used.
TEST(MdMermaidTest, ExplicitQuickJsRendersAndReportsBackend) {
  auto metrics = std::make_shared<RecordingMetrics>();
  MdMermaidEngine engine(metrics);
  ASSERT_TRUE(engine.EnsureLoaded());

  MdMermaidBackend used = MdMermaidBackend::kAuto;
  const MdMermaidResult result =
      engine.Render("graph TD; A[Hello]-->B[World];", MdTheme::Light,
                    MdMermaidBackend::kQuickjs, &used);
  ASSERT_TRUE(result.ok) << result.error;
  EXPECT_EQ(used, MdMermaidBackend::kQuickjs);
  EXPECT_EQ(result.backend_used, MdMermaidBackend::kQuickjs);
}

#endif

#if defined(QLITEMD_MDHTML_MERMAID_MERMAN)

// In a build with merman, auto resolves to the native backend for a
// supported diagram; an invalid diagram fails under every mode.
TEST(MdMermaidTest, AutoPrefersNativeAndReportsIt) {
  auto metrics = std::make_shared<RecordingMetrics>();
  MdMermaidEngine engine(metrics);
  ASSERT_TRUE(engine.EnsureLoaded());

  MdMermaidBackend used = MdMermaidBackend::kQuickjs;
  const MdMermaidResult result =
      engine.Render("graph TD; A[Hello]-->B[World];", MdTheme::Light,
                    MdMermaidBackend::kAuto, &used);
  ASSERT_TRUE(result.ok) << result.error;
  EXPECT_EQ(used, MdMermaidBackend::kNative);
  EXPECT_EQ(result.backend_used, MdMermaidBackend::kNative);
}

TEST(MdMermaidTest, ExplicitNativeRendersAndReportsBackend) {
  auto metrics = std::make_shared<RecordingMetrics>();
  MdMermaidEngine engine(metrics);
  ASSERT_TRUE(engine.EnsureLoaded());

  MdMermaidBackend used = MdMermaidBackend::kAuto;
  const MdMermaidResult result =
      engine.Render("graph TD; A[Hello]-->B[World];", MdTheme::Light,
                    MdMermaidBackend::kNative, &used);
  ASSERT_TRUE(result.ok) << result.error;
  EXPECT_EQ(used, MdMermaidBackend::kNative);
}

TEST(MdMermaidTest, InvalidSourceFailsUnderEveryMode) {
  auto metrics = std::make_shared<RecordingMetrics>();
  MdMermaidEngine engine(metrics);
  ASSERT_TRUE(engine.EnsureLoaded());
  for (const MdMermaidBackend mode :
       {MdMermaidBackend::kAuto, MdMermaidBackend::kNative,
        MdMermaidBackend::kQuickjs}) {
    const MdMermaidResult result =
        engine.Render("this is not mermaid", MdTheme::Light, mode);
    EXPECT_FALSE(result.ok) << static_cast<int>(mode);
  }
}

#endif

#if defined(QLITEMD_MDHTML_MERMAID_MERMAN) && defined(QLITEMD_MDHTML_QUICKJS)

// The whole point of auto: a diagram merman rejects must still render via
// mermaid.js, and the explicit-native request for it must fail.
TEST(MdMermaidTest, AutoFallsBackToQuickJsWhenNativeRejectsDiagram) {
  auto metrics = std::make_shared<RecordingMetrics>();
  MdMermaidEngine engine(metrics);
  ASSERT_TRUE(engine.EnsureLoaded());

  // sankey-beta is not in this merman build's feature set (VERSION.md).
  const char* const code = "sankey-beta\n\nA,B,10\n";
  EXPECT_FALSE(
      engine.Render(code, MdTheme::Light, MdMermaidBackend::kNative).ok);

  MdMermaidBackend used = MdMermaidBackend::kNative;
  const MdMermaidResult result =
      engine.Render(code, MdTheme::Light, MdMermaidBackend::kAuto, &used);
  ASSERT_TRUE(result.ok) << result.error;
  EXPECT_EQ(used, MdMermaidBackend::kQuickjs);
  EXPECT_EQ(result.backend_used, MdMermaidBackend::kQuickjs);
}

#endif

}  // namespace
