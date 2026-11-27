#include "md_block_model.h"

#include <cstdint>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "md_parser.h"

extern "C" {
#include "md4c.h"
}

namespace {

const MdBlock* FindKind(const MdParseResult& parsed, MdBlockKind kind) {
  for (const MdBlock& block : parsed.document_blocks) {
    if (block.kind == kind) {
      return &block;
    }
  }
  return nullptr;
}

int CountKind(const MdParseResult& parsed, MdBlockKind kind) {
  int count = 0;
  for (const MdBlock& block : parsed.document_blocks) {
    if (block.kind == kind) {
      ++count;
    }
  }
  return count;
}

const MdBlock* BlockAt(const MdParseResult& parsed, int ordinal) {
  for (const MdBlock& block : parsed.document_blocks) {
    if (block.ordinal == ordinal) {
      return &block;
    }
  }
  return nullptr;
}

bool HasFlag(const MdBlock& block, MdInlineFlag flag) {
  return (block.inline_flags & static_cast<std::uint32_t>(flag)) != 0;
}

const MdFrontMatterEntry* FindEntry(const MdParseResult& parsed,
                                    const std::string& key) {
  for (const MdFrontMatterEntry& entry : parsed.front_matter) {
    if (entry.key == key) {
      return &entry;
    }
  }
  return nullptr;
}

}  // namespace

TEST(MdBlockModelTest, DirectCollectorMatchesParserPath) {
  const std::string markdown =
      "# H\n\n"
      "para **bold**\n\n"
      "- one\n"
      "- [x] two\n\n"
      "```cpp\n"
      "int x;\n"
      "```\n";

  MdBlockCollectOptions options;
  options.source = markdown;
  options.flags = MD_DIALECT_GITHUB | MD_FLAG_HIGHLIGHT | MD_FLAG_INSERT;
  options.admonition_mode = MdAdmonitionMode::kGithub;
  const std::vector<MdBlock> direct = CollectDocumentBlocks(options);
  const MdParseResult parsed =
      ParseMarkdown(markdown, false, MdAdmonitionMode::kGithub);

  ASSERT_EQ(direct.size(), parsed.document_blocks.size());
  for (std::size_t i = 0; i < direct.size(); ++i) {
    const MdBlock& a = direct[i];
    const MdBlock& b = parsed.document_blocks[i];
    EXPECT_EQ(a.kind, b.kind) << i;
    EXPECT_EQ(a.ordinal, b.ordinal) << i;
    EXPECT_EQ(a.parent, b.parent) << i;
    EXPECT_EQ(a.section, b.section) << i;
    EXPECT_EQ(a.level, b.level) << i;
    EXPECT_EQ(a.admonition, b.admonition) << i;
    EXPECT_EQ(a.mermaid_kind, b.mermaid_kind) << i;
    EXPECT_EQ(a.inline_flags, b.inline_flags) << i;
    EXPECT_EQ(a.children, b.children) << i;
    EXPECT_EQ(a.language, b.language) << i;
  }
}

TEST(MdBlockModelTest, CollectsCoreBlocksWithHierarchy) {
  const MdParseResult parsed = ParseMarkdown(
      "# H1\n\n"
      "para **bold**\n\n"
      "> quote\n\n"
      "- one\n"
      "- [x] two\n\n"
      "```cpp\n"
      "int x;\n"
      "```\n\n"
      "| a | b |\n"
      "|---|---|\n"
      "| 1 | 2 |\n\n"
      "---\n",
      false);

  ASSERT_FALSE(parsed.document_blocks.empty());
  for (std::size_t i = 0; i < parsed.document_blocks.size(); ++i) {
    EXPECT_EQ(parsed.document_blocks[i].ordinal, static_cast<int>(i));
  }

  const MdBlock* heading = FindKind(parsed, MdBlockKind::kHeading);
  ASSERT_NE(heading, nullptr);
  EXPECT_EQ(heading->level, 1);
  EXPECT_EQ(heading->section, 0);
  EXPECT_EQ(heading->parent, -1);

  const MdBlock* paragraph = FindKind(parsed, MdBlockKind::kParagraph);
  ASSERT_NE(paragraph, nullptr);
  EXPECT_TRUE(HasFlag(*paragraph, kInlineBold));

  const MdBlock* quote = FindKind(parsed, MdBlockKind::kBlockQuote);
  ASSERT_NE(quote, nullptr);
  const MdBlock* quote_child = FindKind(parsed, MdBlockKind::kParagraph);
  ASSERT_NE(quote_child, nullptr);
  bool saw_quote_child = false;
  for (const MdBlock& block : parsed.document_blocks) {
    if (block.kind == MdBlockKind::kParagraph &&
        block.parent == quote->ordinal) {
      saw_quote_child = true;
    }
  }
  EXPECT_TRUE(saw_quote_child);

  const MdBlock* list = FindKind(parsed, MdBlockKind::kList);
  ASSERT_NE(list, nullptr);
  EXPECT_EQ(list->children, 2);
  int list_items = 0;
  for (const MdBlock& block : parsed.document_blocks) {
    if ((block.kind == MdBlockKind::kListItem ||
         block.kind == MdBlockKind::kTaskItem) &&
        block.parent == list->ordinal) {
      ++list_items;
    }
  }
  EXPECT_EQ(list_items, 2);
  EXPECT_EQ(CountKind(parsed, MdBlockKind::kTaskItem), 1);

  const MdBlock* code = FindKind(parsed, MdBlockKind::kCodeBlock);
  ASSERT_NE(code, nullptr);
  EXPECT_EQ(code->language, "cpp");

  const MdBlock* table = FindKind(parsed, MdBlockKind::kTable);
  ASSERT_NE(table, nullptr);
  EXPECT_EQ(table->children, 2);

  EXPECT_NE(FindKind(parsed, MdBlockKind::kThematicBreak), nullptr);

  for (const MdBlock& block : parsed.document_blocks) {
    EXPECT_LT(block.parent, block.ordinal);
  }
  const MdBlock* first = BlockAt(parsed, 0);
  ASSERT_NE(first, nullptr);
  EXPECT_EQ(first->parent, -1);
}

TEST(MdBlockModelTest, HeadingsOpenSections) {
  const MdParseResult parsed =
      ParseMarkdown("intro\n\n# A\n\na\n\n## B\n\nb\n", false);

  ASSERT_EQ(parsed.document_blocks.size(), 5u);
  EXPECT_EQ(parsed.document_blocks[0].kind, MdBlockKind::kParagraph);
  EXPECT_EQ(parsed.document_blocks[0].section, -1);
  EXPECT_EQ(parsed.document_blocks[1].kind, MdBlockKind::kHeading);
  EXPECT_EQ(parsed.document_blocks[1].section, 0);
  EXPECT_EQ(parsed.document_blocks[2].section, 0);
  EXPECT_EQ(parsed.document_blocks[3].kind, MdBlockKind::kHeading);
  EXPECT_EQ(parsed.document_blocks[3].section, 1);
  EXPECT_EQ(parsed.document_blocks[4].section, 1);
}

TEST(MdBlockModelTest, CollectsInlineFlags) {
  const MdParseResult parsed = ParseMarkdown(
      "**b** *i* ~~s~~ `c` ==m== H~2~O 2^10^ ++ins++ "
      "[l](https://example.com) ![alt](x.png)\n",
      false);

  const MdBlock* paragraph = FindKind(parsed, MdBlockKind::kParagraph);
  ASSERT_NE(paragraph, nullptr);
  EXPECT_TRUE(HasFlag(*paragraph, kInlineBold));
  EXPECT_TRUE(HasFlag(*paragraph, kInlineItalic));
  EXPECT_TRUE(HasFlag(*paragraph, kInlineStrike));
  EXPECT_TRUE(HasFlag(*paragraph, kInlineCode));
  EXPECT_TRUE(HasFlag(*paragraph, kInlineMark));
  EXPECT_TRUE(HasFlag(*paragraph, kInlineInsert));
  EXPECT_TRUE(HasFlag(*paragraph, kInlineLink));
  EXPECT_TRUE(HasFlag(*paragraph, kInlineImage));
}

TEST(MdBlockModelTest, SuperscriptAndSubscriptStayOffForLiteralMath) {
  const MdParseResult parsed = ParseMarkdown("$x^2$ and H~2~O\n", false);

  EXPECT_NE(parsed.html.find("$x^2$"), std::string::npos);
  EXPECT_NE(parsed.html.find("H~2~O"), std::string::npos);
  const MdBlock* paragraph = FindKind(parsed, MdBlockKind::kParagraph);
  ASSERT_NE(paragraph, nullptr);
  EXPECT_FALSE(HasFlag(*paragraph, kInlineSup));
  EXPECT_FALSE(HasFlag(*paragraph, kInlineSub));
}

TEST(MdBlockModelTest, GithubAdmonitionKindIsPreserved) {
  const MdParseResult parsed =
      ParseMarkdown("> [!WARNING]\n> body\n", false, MdAdmonitionMode::kGithub);

  const MdBlock* admonition = FindKind(parsed, MdBlockKind::kAdmonition);
  ASSERT_NE(admonition, nullptr);
  EXPECT_EQ(admonition->admonition, MdAdmonitionKind::kWarning);
  EXPECT_EQ(admonition->parent, -1);
}

TEST(MdBlockModelTest, MpeCalloutBecomesAdmonition) {
  const MdParseResult parsed = ParseMarkdown("> [!danger] 危险\n> body\n",
                                             false, MdAdmonitionMode::kMpe);

  const MdBlock* admonition = FindKind(parsed, MdBlockKind::kAdmonition);
  ASSERT_NE(admonition, nullptr);
  EXPECT_EQ(admonition->admonition, MdAdmonitionKind::kDanger);
}

TEST(MdBlockModelTest, NormalQuoteIsNotAdmonition) {
  const MdParseResult parsed =
      ParseMarkdown("> just a quote\n", false, MdAdmonitionMode::kMpe);

  EXPECT_EQ(FindKind(parsed, MdBlockKind::kBlockQuote) != nullptr, true);
  EXPECT_EQ(FindKind(parsed, MdBlockKind::kAdmonition), nullptr);
}

TEST(MdBlockModelTest, MermaidDiagramKindIsNormalized) {
  const MdParseResult parsed = ParseMarkdown(
      "```mermaid\n"
      "sequenceDiagram\n"
      "  A->>B: hi\n"
      "```\n",
      false);

  const MdBlock* mermaid = FindKind(parsed, MdBlockKind::kMermaid);
  ASSERT_NE(mermaid, nullptr);
  EXPECT_EQ(mermaid->language, "mermaid");
  EXPECT_EQ(mermaid->mermaid_kind, MdMermaidDiagram::kSequence);
}

TEST(MdBlockModelTest, MathPlaceholdersBecomeBlocksAndFlags) {
  const MdParseResult parsed = ParseMarkdown(
      "$$\n"
      "x^2\n"
      "$$\n\n"
      "inline $y$ here\n",
      true);

  ASSERT_GE(parsed.document_blocks.size(), 2u);
  EXPECT_EQ(parsed.document_blocks[0].kind, MdBlockKind::kMathBlock);
  const MdBlock* paragraph = nullptr;
  for (const MdBlock& block : parsed.document_blocks) {
    if (block.kind == MdBlockKind::kParagraph) {
      paragraph = &block;
    }
  }
  ASSERT_NE(paragraph, nullptr);
  EXPECT_TRUE(HasFlag(*paragraph, kInlineMath));
}

TEST(MdBlockModelTest, MathIsPlainTextWithoutExtraction) {
  const MdParseResult parsed = ParseMarkdown(
      "$$\n"
      "x^2\n"
      "$$\n",
      false);

  EXPECT_EQ(FindKind(parsed, MdBlockKind::kMathBlock), nullptr);
}

TEST(MdBlockModelTest, FrontMatterIsExtractedAndNotRendered) {
  const MdParseResult parsed = ParseMarkdown(
      "---\n"
      "title: 测试文档\n"
      "tags: a, b\n"
      "---\n"
      "# H\n",
      false);

  ASSERT_EQ(parsed.front_matter.size(), 2u);
  const MdFrontMatterEntry* title = FindEntry(parsed, "title");
  ASSERT_NE(title, nullptr);
  EXPECT_EQ(title->value, "测试文档");
  const MdFrontMatterEntry* tags = FindEntry(parsed, "tags");
  ASSERT_NE(tags, nullptr);
  EXPECT_EQ(tags->value, "a, b");
  EXPECT_EQ(parsed.html.find("title:"), std::string::npos);
  EXPECT_NE(parsed.html.find("<h1>"), std::string::npos);
}

TEST(MdBlockModelTest, MidDocumentRuleIsNotFrontMatter) {
  const MdParseResult parsed = ParseMarkdown("text\n\n---\n\nmore\n", false);

  EXPECT_TRUE(parsed.front_matter.empty());
  EXPECT_NE(parsed.html.find("<hr"), std::string::npos);
}

TEST(MdBlockModelTest, UnclosedFrontMatterFallsBackToMarkdown) {
  const MdParseResult parsed = ParseMarkdown("---\ntitle: x\n# H\n", false);

  EXPECT_TRUE(parsed.front_matter.empty());
  EXPECT_NE(parsed.html.find("title: x"), std::string::npos);
}

TEST(MdBlockModelTest, RawHtmlBecomesHtmlBlock) {
  const MdParseResult parsed =
      ParseMarkdown("<details><summary>x</summary>y</details>\n", false);

  EXPECT_NE(FindKind(parsed, MdBlockKind::kHtmlBlock), nullptr);
}

TEST(MdBlockModelTest, FootnoteDefinitionIsEmitted) {
  const MdParseResult parsed = ParseMarkdown("text[^1]\n\n[^1]: note\n", false);

  EXPECT_NE(FindKind(parsed, MdBlockKind::kFootnoteDef), nullptr);
}

TEST(MdBlockModelTest, ImageOnlyParagraphBecomesImageBlock) {
  const MdParseResult parsed = ParseMarkdown("![alt](x.png)\n", false);

  EXPECT_NE(FindKind(parsed, MdBlockKind::kImage), nullptr);
  EXPECT_EQ(FindKind(parsed, MdBlockKind::kParagraph), nullptr);
}
