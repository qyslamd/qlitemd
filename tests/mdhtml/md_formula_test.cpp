#include "md_formula.h"

#include <string>

#include <gtest/gtest.h>

namespace {

#if defined(QLITEMD_MDHTML_QUICKJS)

TEST(MdFormulaTest, AutoRendersSvgAndReportsBackend) {
  MdFormulaEngine engine;
  MdFormulaBackend used = MdFormulaBackend::kQuickjs;
  const MdFormulaResult result =
      engine.Render("x^2", false, MdColor{0xff, 0x00, 0x00, 0xff},
                    MdFormulaBackend::kAuto, &used);
  ASSERT_TRUE(result.ok) << result.error;
  EXPECT_EQ(result.svg.svg.rfind("<svg", 0), 0u) << result.svg.svg;
  EXPECT_GT(result.svg.width, 0);
  EXPECT_GT(result.svg.height, 0);
  EXPECT_EQ(used, result.backend_used);
#if defined(QLITEMD_MDHTML_FORMULA_RATEX)
  EXPECT_EQ(used, MdFormulaBackend::kNative);
#else
  EXPECT_EQ(used, MdFormulaBackend::kQuickjs);
#endif
}

TEST(MdFormulaTest, DisplayModeChangesMetrics) {
  MdFormulaEngine engine;
  const MdFormulaResult inline_result =
      engine.Render("\\sum_{i=0}^n i", false, MdColor{});
  const MdFormulaResult display_result =
      engine.Render("\\sum_{i=0}^n i", true, MdColor{});
  ASSERT_TRUE(inline_result.ok) << inline_result.error;
  ASSERT_TRUE(display_result.ok) << display_result.error;
  EXPECT_GT(display_result.svg.height, inline_result.svg.height);
}

TEST(MdFormulaTest, EmptyTexFails) {
  MdFormulaEngine engine;
  EXPECT_FALSE(engine.Render("   ", false, MdColor{}).ok);
}

TEST(MdFormulaTest, ExplicitQuickJsRendersAndReportsBackend) {
  MdFormulaEngine engine;
  MdFormulaBackend used = MdFormulaBackend::kAuto;
  const MdFormulaResult result =
      engine.Render("x^2", false, MdColor{0xff, 0x00, 0x00, 0xff},
                    MdFormulaBackend::kQuickjs, &used);
  ASSERT_TRUE(result.ok) << result.error;
  EXPECT_EQ(used, MdFormulaBackend::kQuickjs);
  EXPECT_EQ(result.backend_used, MdFormulaBackend::kQuickjs);
  EXPECT_NE(result.svg.svg.find("#ff0000"), std::string::npos)
      << result.svg.svg;
}

#if defined(QLITEMD_MDHTML_FORMULA_RATEX)

TEST(MdFormulaTest, ExplicitNativeRendersAndReportsBackend) {
  MdFormulaEngine engine;
  MdFormulaBackend used = MdFormulaBackend::kAuto;
  const MdFormulaResult result =
      engine.Render("x^2", false, MdColor{0xff, 0x00, 0x00, 0xff},
                    MdFormulaBackend::kNative, &used);
  ASSERT_TRUE(result.ok) << result.error;
  EXPECT_EQ(used, MdFormulaBackend::kNative);
  EXPECT_EQ(result.backend_used, MdFormulaBackend::kNative);
  EXPECT_NE(result.svg.svg.find("#ff0000"), std::string::npos)
      << result.svg.svg;
}

#endif

#if defined(QLITEMD_MDHTML_FORMULA_RATEX)

// The whole point of auto: a formula RaTeX rejects must still render via
// MathJax, and the explicit-native request for it must fail.
TEST(MdFormulaTest, AutoFallsBackToQuickJsWhenNativeRejectsFormula) {
  MdFormulaEngine engine;
  const char* const tex = "\\bbox[5px,border:1px solid red]{x}";
  EXPECT_FALSE(
      engine.Render(tex, false, MdColor{}, MdFormulaBackend::kNative).ok);

  MdFormulaBackend used = MdFormulaBackend::kNative;
  const MdFormulaResult result =
      engine.Render(tex, false, MdColor{}, MdFormulaBackend::kAuto, &used);
  ASSERT_TRUE(result.ok) << result.error;
  EXPECT_EQ(used, MdFormulaBackend::kQuickjs);
  EXPECT_EQ(result.backend_used, MdFormulaBackend::kQuickjs);
}

#endif

#endif  // QLITEMD_MDHTML_QUICKJS

TEST(MdFormulaTest, RenderKeyNormalizesWhitespaceAndColor) {
  EXPECT_EQ(MdFormulaRenderKey(" x^2 ", false, MdColor{0, 0, 0, 255}),
            MdFormulaRenderKey("x^2", false, MdColor{0, 0, 0, 255}));
  EXPECT_NE(MdFormulaRenderKey("x^2", false, MdColor{0, 0, 0, 255}),
            MdFormulaRenderKey("x^2", true, MdColor{0, 0, 0, 255}));
  EXPECT_NE(MdFormulaRenderKey("x^2", false, MdColor{0, 0, 0, 255}),
            MdFormulaRenderKey("x^2", false, MdColor{255, 255, 255, 255}));
}

TEST(MdFormulaTest, RenderKeySeparatesBackendModes) {
  const std::string auto_key =
      MdFormulaRenderKey("x^2", false, MdColor{}, MdFormulaBackend::kAuto);
  const std::string native_key =
      MdFormulaRenderKey("x^2", false, MdColor{}, MdFormulaBackend::kNative);
  const std::string quickjs_key =
      MdFormulaRenderKey("x^2", false, MdColor{}, MdFormulaBackend::kQuickjs);
  EXPECT_NE(auto_key, native_key);
  EXPECT_NE(auto_key, quickjs_key);
  EXPECT_NE(native_key, quickjs_key);
}

}  // namespace
