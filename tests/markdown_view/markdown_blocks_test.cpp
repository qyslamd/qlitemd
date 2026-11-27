#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QStandardPaths>

#include <gtest/gtest.h>

#include "markdown_test_wait.h"
#include "markdown_view.h"

// Verifies that the Qt view exposes the same structural blocks the core
// produced, after a real offscreen render.
int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  qunsetenv("MD_MARKDOWN_CSS");
  QStandardPaths::setTestModeEnabled(true);
  QCoreApplication::setOrganizationName(QStringLiteral("md-tests"));
  QCoreApplication::setApplicationName(
      QStringLiteral("blocks-%1").arg(QDateTime::currentMSecsSinceEpoch()));
  QApplication app(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  const int result = RUN_ALL_TESTS();
  QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation) +
       QStringLiteral("/markdown-render"))
      .removeRecursively();
  return result;
}

namespace {

TEST(MarkdownBlocksTest, ExposesRenderedBlocks) {
  MarkdownView view;
  RenderWaiter wait(&view);
  view.SetContent(
      QStringLiteral("# Heading\n\n"
                     "text **bold**\n\n"
                     "- [x] task\n\n"
                     "```mermaid\nsequenceDiagram\n  A->>B: hi\n```\n"),
      QString());
  ASSERT_TRUE(wait.Wait());

  const QVector<MarkdownBlock>& blocks = view.blocks();
  ASSERT_GE(blocks.size(), 4);

  bool saw_heading = false;
  bool saw_task = false;
  bool saw_mermaid = false;
  for (int i = 0; i < blocks.size(); ++i) {
    const MarkdownBlock& block = blocks[i];
    EXPECT_EQ(block.ordinal, i);
    EXPECT_LT(block.parent, block.ordinal);
    if (block.kind == MarkdownBlockKind::kHeading) {
      saw_heading = true;
      EXPECT_EQ(block.level, 1);
      EXPECT_EQ(block.section, 0);
    }
    if (block.kind == MarkdownBlockKind::kTaskItem) {
      saw_task = true;
    }
    if (block.kind == MarkdownBlockKind::kMermaid) {
      saw_mermaid = true;
      EXPECT_EQ(block.language, QStringLiteral("mermaid"));
      EXPECT_EQ(block.mermaid_kind, MarkdownMermaidDiagram::kSequence);
    }
  }
  EXPECT_TRUE(saw_heading);
  EXPECT_TRUE(saw_task);
  EXPECT_TRUE(saw_mermaid);
}

TEST(MarkdownBlocksTest, ClearsOnPlaceholderDocument) {
  MarkdownView view;
  RenderWaiter wait(&view);
  view.SetContent(QStringLiteral("# H\n"), QString());
  ASSERT_TRUE(wait.Wait());
  ASSERT_FALSE(view.blocks().isEmpty());

  RenderWaiter second(&view);
  view.SetContent(QString(), QString());
  ASSERT_TRUE(second.Wait());
  EXPECT_TRUE(view.blocks().isEmpty());
}

}  // namespace
