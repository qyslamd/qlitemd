#include <gtest/gtest.h>

#include "markdown_render_context.h"
#include "markdown_render_context_internal.h"

namespace {

// 对外 options 与实际缓存上限必须一致：默认构造的上下文不做 SetOptions，
// 上限也要按 MarkdownRenderContextOptions 的默认值生效
TEST(RenderContextTest, DefaultOptionsDriveCacheLimits) {
  MarkdownRenderContext context;
  MarkdownRenderContextState& state =
      MarkdownRenderContextAccess::State(context);
  EXPECT_EQ(state.highlight_cache.max_entries,
            context.options().highlight_cache_entries);
  EXPECT_EQ(state.formula_cache.max_entries,
            context.options().formula_cache_entries);
  EXPECT_EQ(state.mermaid_cache.max_entries,
            context.options().mermaid_cache_entries);
}

TEST(RenderContextTest, SetOptionsUpdatesCacheLimits) {
  MarkdownRenderContext context;
  MarkdownRenderContextOptions options;
  options.highlight_cache_entries = 11;
  options.formula_cache_entries = 7;
  options.mermaid_cache_entries = 3;
  context.SetOptions(options);

  MarkdownRenderContextState& state =
      MarkdownRenderContextAccess::State(context);
  EXPECT_EQ(state.highlight_cache.max_entries, 11);
  EXPECT_EQ(state.formula_cache.max_entries, 7);
  EXPECT_EQ(state.mermaid_cache.max_entries, 3);
}

TEST(RenderContextTest, SetOptionsTrimsExistingEntriesImmediately) {
  MarkdownRenderContext context;
  MarkdownRenderContextOptions options;
  options.highlight_cache_entries = 4;
  options.formula_cache_entries = 4;
  context.SetOptions(options);
  MarkdownRenderContextState& state =
      MarkdownRenderContextAccess::State(context);
  for (int i = 0; i < 4; ++i) {
    state.highlight_cache.Insert(QString::number(i), QStringLiteral("v"));
    state.formula_cache.Insert(QString::number(i), QStringLiteral("v"));
  }
  EXPECT_EQ(state.highlight_cache.entries.size(), 4);
  EXPECT_EQ(state.formula_cache.entries.size(), 4);

  options.highlight_cache_entries = 2;
  options.formula_cache_entries = 2;
  context.SetOptions(options);
  EXPECT_EQ(state.highlight_cache.entries.size(), 2);
  EXPECT_EQ(state.formula_cache.entries.size(), 2);
}

// ResetStats 只清计数，不动配置与上限
TEST(RenderContextTest, ResetStatsKeepsOptions) {
  MarkdownRenderContext context;
  MarkdownRenderContextOptions options;
  options.highlight_cache_entries = 6;
  options.formula_cache_entries = 9;
  options.mermaid_cache_entries = 4;
  context.SetOptions(options);
  context.ResetStats();

  EXPECT_EQ(context.options().highlight_cache_entries, 6);
  EXPECT_EQ(context.options().formula_cache_entries, 9);
  MarkdownRenderContextState& state =
      MarkdownRenderContextAccess::State(context);
  EXPECT_EQ(state.highlight_cache.max_entries, 6);
  EXPECT_EQ(state.formula_cache.max_entries, 9);
  EXPECT_EQ(state.mermaid_cache.max_entries, 4);
}

}  // namespace
