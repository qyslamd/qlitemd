#include "md_formula.h"

#include <string>

#include <gtest/gtest.h>

namespace {

#if defined(QLITEMD_MDHTML_FORMULA_RATEX) || \
    defined(QLITEMD_MDHTML_FORMULA_QUICKJS)

TEST(MdFormulaTest, RendersInlineSvgWithColor) {
  MdFormulaEngine engine;
  const MdFormulaResult result =
      engine.Render("x^2", false, MdColor{0xff, 0x00, 0x00, 0xff});
  ASSERT_TRUE(result.ok) << result.error;
  EXPECT_EQ(result.svg.svg.rfind("<svg", 0), 0u) << result.svg.svg;
  EXPECT_GT(result.svg.width, 0);
  EXPECT_GT(result.svg.height, 0);
#if defined(QLITEMD_MDHTML_FORMULA_RATEX)
  EXPECT_NE(result.svg.svg.find("rgba(255,0,0,1)"), std::string::npos)
      << result.svg.svg;
#else
  EXPECT_NE(result.svg.svg.find("#ff0000"), std::string::npos)
      << result.svg.svg;
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

#else

TEST(MdFormulaTest, MissingBackendFailsGracefully) {
  MdFormulaEngine engine;
  const MdFormulaResult result = engine.Render("x^2", false, MdColor{});
  EXPECT_FALSE(result.ok);
  EXPECT_FALSE(result.error.empty());
}

#endif

TEST(MdFormulaTest, RenderKeyNormalizesWhitespaceAndColor) {
  EXPECT_EQ(MdFormulaRenderKey(" x^2 ", false, MdColor{0, 0, 0, 255}),
            MdFormulaRenderKey("x^2", false, MdColor{0, 0, 0, 255}));
  EXPECT_NE(MdFormulaRenderKey("x^2", false, MdColor{0, 0, 0, 255}),
            MdFormulaRenderKey("x^2", true, MdColor{0, 0, 0, 255}));
  EXPECT_NE(MdFormulaRenderKey("x^2", false, MdColor{0, 0, 0, 255}),
            MdFormulaRenderKey("x^2", false, MdColor{255, 255, 255, 255}));
}

}  // namespace
