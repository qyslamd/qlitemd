#include "md_svg.h"

#include <string>

#include <gtest/gtest.h>

#include "md_hash.h"
#include "md_types.h"

namespace {

constexpr const char* kSample =
    "<?xml version=\"1.0\"?><svg xmlns=\"http://www.w3.org/2000/svg\" "
    "viewBox=\"0 0 320 48\"><g><text>x</text></g></svg>trailing";

TEST(MdSvgTest, ExtractsRootElement) {
  const std::string root = MdExtractSvgRoot(kSample);
  EXPECT_EQ(root.rfind("<svg", 0), 0u);
  EXPECT_EQ(root.size() - 6, root.rfind("</svg>"));
}

TEST(MdSvgTest, MissingRootIsRejected) {
  EXPECT_TRUE(MdExtractSvgRoot("not an svg").empty());
  EXPECT_TRUE(MdExtractSvgRoot("<svg>").empty());
}

TEST(MdSvgTest, OversizedSvgIsRejected) {
  std::string huge(kMdMaxSvgBytes + 1, ' ');
  huge.replace(0, 5, "<svg>");
  huge.replace(huge.size() - 6, 6, "</svg>");
  EXPECT_TRUE(MdExtractSvgRoot(huge).empty());
}

TEST(MdSvgTest, SizeComesFromViewBox) {
  int width = 0;
  int height = 0;
  ASSERT_TRUE(MdSvgIntrinsicSize(kSample, &width, &height));
  EXPECT_EQ(width, 320);
  EXPECT_EQ(height, 48);
}

TEST(MdSvgTest, SizeFallsBackToPixelAttributes) {
  int width = 0;
  int height = 0;
  ASSERT_TRUE(MdSvgIntrinsicSize(
      "<svg width=\"120px\" height=\"30\"><g/></svg>", &width, &height));
  EXPECT_EQ(width, 120);
  EXPECT_EQ(height, 30);
}

TEST(MdSvgTest, PercentSizesAreNotIntrinsic) {
  int width = 0;
  int height = 0;
  EXPECT_FALSE(MdSvgIntrinsicSize("<svg width=\"100%\" height=\"100%\"/>",
                                  &width, &height));
}

TEST(MdSvgTest, ColorReplacesCurrentColor) {
  const std::string colored =
      MdApplySvgColor("<svg><g fill=\"currentColor\"><path/></g></svg>",
                      MdColor{0x11, 0x22, 0x33, 0xff});
  EXPECT_NE(colored.find("#112233"), std::string::npos) << colored;
  EXPECT_EQ(colored.find("currentColor"), std::string::npos) << colored;
}

TEST(MdSvgTest, ColorInjectsFillOnRootGroup) {
  const std::string colored = MdApplySvgColor("<svg><g><path/></g></svg>",
                                              MdColor{0xab, 0xcd, 0xef, 0xff});
  EXPECT_NE(colored.find("<g fill=\"#abcdef\">"), std::string::npos) << colored;
}

TEST(MdSvgTest, ColorDoesNotDuplicateExistingFill) {
  const std::string svg = "<svg><g fill=\"#000000\"><path/></g></svg>";
  EXPECT_EQ(MdApplySvgColor(svg, MdColor{0xff, 0, 0, 0xff}), svg);
}

TEST(MdSvgTest, PaintPortableConvertsRgbaPaint) {
  const std::string svg = MdSvgMakePaintPortable(
      "<svg><path fill=\"rgba(255, 0, 128, 1)\" stroke=\"rgba(0,0,0,0.5)\"/>"
      "</svg>");
  EXPECT_NE(svg.find("fill=\"#ff0080\""), std::string::npos) << svg;
  EXPECT_NE(svg.find("stroke=\"#000000\""), std::string::npos) << svg;
  EXPECT_EQ(svg.find("rgba("), std::string::npos) << svg;
}

TEST(MdHashTest, Sha256KnownVectors) {
  EXPECT_EQ(MdSha256Hex(""),
            "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
  EXPECT_EQ(MdSha256Hex("abc"),
            "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

TEST(MdHashTest, AssetIdIsStableAndKindSeparated) {
  const std::string formula = MdAssetId(MdAssetKind::Formula, "x^2");
  const std::string mermaid = MdAssetId(MdAssetKind::Mermaid, "x^2");
  EXPECT_EQ(formula, MdAssetId(MdAssetKind::Formula, "x^2"));
  EXPECT_NE(formula, mermaid);
  EXPECT_EQ(formula.size(), 32u);
  EXPECT_EQ(mermaid.size(), 32u);
}

}  // namespace
