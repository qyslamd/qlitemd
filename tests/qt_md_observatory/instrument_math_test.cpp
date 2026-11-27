#include "instrument_math.h"

#include <gtest/gtest.h>

namespace {

MarkdownBlock Block(MarkdownBlockKind kind, int section = -1) {
  MarkdownBlock block;
  block.kind = kind;
  block.section = section;
  return block;
}

MarkdownHeading Heading(const QString& title, int level,
                        const QString& anchor) {
  MarkdownHeading heading;
  heading.text = title;
  heading.level = level;
  heading.anchor = anchor;
  return heading;
}

}  // namespace

TEST(InstrumentMathTest, GroupsBlocksIntoComposition) {
  QVector<MarkdownBlock> blocks = {
      Block(MarkdownBlockKind::kHeading),
      Block(MarkdownBlockKind::kParagraph),
      Block(MarkdownBlockKind::kBlockQuote),
      Block(MarkdownBlockKind::kList),
      Block(MarkdownBlockKind::kListItem),
      Block(MarkdownBlockKind::kTaskItem),
      Block(MarkdownBlockKind::kCodeBlock),
      Block(MarkdownBlockKind::kMathBlock),
      Block(MarkdownBlockKind::kMermaid),
      Block(MarkdownBlockKind::kTable),
      Block(MarkdownBlockKind::kAdmonition),
      Block(MarkdownBlockKind::kFootnoteDef),
      Block(MarkdownBlockKind::kThematicBreak),
      Block(MarkdownBlockKind::kHtmlBlock),
  };
  const InstrumentData data = BuildInstrumentData(blocks, {}, {}, 0, 1000, 200);

  ASSERT_EQ(data.composition.size(), kBlockGroupCount);
  EXPECT_EQ(data.composition[static_cast<int>(BlockGroup::kHeading)], 1);
  EXPECT_EQ(data.composition[static_cast<int>(BlockGroup::kText)], 4);
  EXPECT_EQ(data.composition[static_cast<int>(BlockGroup::kCode)], 1);
  EXPECT_EQ(data.composition[static_cast<int>(BlockGroup::kMath)], 1);
  EXPECT_EQ(data.composition[static_cast<int>(BlockGroup::kDiagram)], 1);
  EXPECT_EQ(data.composition[static_cast<int>(BlockGroup::kTable)], 1);
  EXPECT_EQ(data.composition[static_cast<int>(BlockGroup::kNote)], 2);
  EXPECT_EQ(data.composition[static_cast<int>(BlockGroup::kRule)], 2);
  EXPECT_EQ(data.total_blocks, 13);
}

TEST(InstrumentMathTest, SectionsFollowHeadingsAndPreamble) {
  QVector<MarkdownBlock> blocks = {
      Block(MarkdownBlockKind::kParagraph, -1),
      Block(MarkdownBlockKind::kHeading, 0),
      Block(MarkdownBlockKind::kParagraph, 0),
      Block(MarkdownBlockKind::kHeading, 1),
      Block(MarkdownBlockKind::kCodeBlock, 1),
  };
  QVector<MarkdownHeading> outline = {
      Heading(QStringLiteral("A"), 1, QStringLiteral("a")),
      Heading(QStringLiteral("B"), 2, QStringLiteral("b")),
  };
  QVector<int> positions = {0, 500};

  const InstrumentData data =
      BuildInstrumentData(blocks, outline, positions, 0, 1000, 200);
  ASSERT_EQ(data.sections.size(), 2);
  EXPECT_EQ(data.sections[0].title, QStringLiteral("A"));
  EXPECT_EQ(data.sections[0].block_count, 2);
  EXPECT_EQ(data.sections[1].block_count, 2);
  EXPECT_EQ(data.total_blocks, 5);
  EXPECT_EQ(data.current_section, 0);

  const InstrumentData scrolled =
      BuildInstrumentData(blocks, outline, positions, 520, 1000, 200);
  EXPECT_EQ(scrolled.current_section, 1);
  EXPECT_EQ(scrolled.progress, 65);
}

TEST(InstrumentMathTest, CurrentSectionStaysUnknownWithoutPositions) {
  QVector<MarkdownHeading> outline = {
      Heading(QStringLiteral("A"), 1, QStringLiteral("a")),
  };
  const InstrumentData data =
      BuildInstrumentData({}, outline, {-1}, 0, 1000, 200);
  EXPECT_EQ(data.current_section, -1);
}

TEST(InstrumentMathTest, ProgressClampsAndHandlesShortDocuments) {
  EXPECT_EQ(ComputeProgress(0, 1000, 200), 0);
  EXPECT_EQ(ComputeProgress(400, 1000, 200), 50);
  EXPECT_EQ(ComputeProgress(900, 1000, 200), 100);
  EXPECT_EQ(ComputeProgress(-5, 1000, 200), 0);
  EXPECT_EQ(ComputeProgress(10, 100, 200), 100);
}

TEST(InstrumentMathTest, GroupLabelsAreStable) {
  EXPECT_EQ(BlockGroupLabel(BlockGroup::kHeading), QStringLiteral("Headings"));
  EXPECT_EQ(BlockGroupLabel(BlockGroup::kNote), QStringLiteral("Notes"));
  EXPECT_EQ(BlockGroupColorHex(BlockGroup::kCode), QStringLiteral("#5b8a72"));
}
