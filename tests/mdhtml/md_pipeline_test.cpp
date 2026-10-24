#include "md_pipeline.h"

#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace {

constexpr const char* kDocument =
    "# Title\n\n"
    "```cpp\n"
    "class A {};\n"
    "```\n\n"
    "Inline $x^2$ and again $x^2$.\n\n"
    "```mermaid\n"
    "graph TD; A-->B;\n"
    "```\n";

MdHtmlPipeline CreatePipeline(std::size_t workers = 2) {
  MdPipelineOptions options;
  options.highlight_workers = workers;
  options.formula_workers = workers;
  options.mermaid_workers = workers;
  options.queue_capacity = 8;
  return MdHtmlPipeline(options);
}

TEST(MdPipelineTest, RendersDeterministically) {
  MdHtmlPipeline pipeline = CreatePipeline();
  const MdHtmlResult first = pipeline.Render(kDocument, MdRenderOptions{});
  const MdHtmlResult second = pipeline.Render(kDocument, MdRenderOptions{});
  ASSERT_TRUE(first.ok) << first.error;
  ASSERT_TRUE(second.ok) << second.error;
  EXPECT_EQ(first.html, second.html);
  ASSERT_EQ(first.assets.size(), second.assets.size());
  EXPECT_NE(first.html.find("qlitemd-asset:"), std::string::npos);
  EXPECT_NE(first.html.find("class=\"mermaid\""), std::string::npos);
  EXPECT_NE(first.html.find("class=\"kw\""), std::string::npos);
}

TEST(MdPipelineTest, DeduplicatesRepeatedSources) {
  MdHtmlPipeline pipeline = CreatePipeline(1);
  const MdHtmlResult result = pipeline.Render(kDocument, MdRenderOptions{});
  ASSERT_TRUE(result.ok) << result.error;
  // $x^2$ appears twice but renders once; the Mermaid block is one asset.
  EXPECT_EQ(result.assets.size(), 2u);
  EXPECT_EQ(result.stats.formulas, 1);
  EXPECT_EQ(result.stats.mermaid_blocks, 1);
  EXPECT_EQ(result.stats.worker_failures, 0);
}

TEST(MdPipelineTest, DisabledMermaidFallsBackToCodeBlock) {
  MdHtmlPipeline pipeline = CreatePipeline(1);
  MdRenderOptions options;
  options.mermaid = false;
  const MdHtmlResult result = pipeline.Render(kDocument, options);
  ASSERT_TRUE(result.ok) << result.error;
  EXPECT_EQ(result.html.find("mermaid-wrap"), std::string::npos);
  EXPECT_NE(result.html.find("code-lang\">mermaid"), std::string::npos);
  for (const MdAsset& asset : result.assets) {
    EXPECT_NE(asset.kind, MdAssetKind::Mermaid);
  }
}

TEST(MdPipelineTest, DisabledFormulaLeavesSourceLiteral) {
  MdHtmlPipeline pipeline = CreatePipeline(1);
  MdRenderOptions options;
  options.formula = false;
  const MdHtmlResult result = pipeline.Render(kDocument, options);
  ASSERT_TRUE(result.ok) << result.error;
  EXPECT_NE(result.html.find("$x^2$"), std::string::npos);
  for (const MdAsset& asset : result.assets) {
    EXPECT_NE(asset.kind, MdAssetKind::Formula);
  }
}

TEST(MdPipelineTest, ProgressIsMonotonicAndFinishes) {
  MdHtmlPipeline pipeline = CreatePipeline(1);
  std::mutex mutex;
  std::vector<std::pair<int, int>> snapshots;
  const std::shared_ptr<MdRenderJob> job =
      pipeline.RenderAsync(kDocument, MdRenderOptions{},
                           [&](int completed, int total, int in_flight) {
                             std::lock_guard<std::mutex> lock(mutex);
                             snapshots.emplace_back(completed, total);
                             EXPECT_EQ(completed + in_flight, total);
                           });
  const MdHtmlResult result = job->Wait();
  ASSERT_TRUE(result.ok) << result.error;
  ASSERT_FALSE(snapshots.empty());
  int previous = -1;
  for (const auto& snapshot : snapshots) {
    EXPECT_GE(snapshot.first, previous);
    previous = snapshot.first;
  }
  EXPECT_EQ(snapshots.back().first, snapshots.back().second);
}

TEST(MdPipelineTest, CancelMarksJobCancelled) {
  MdHtmlPipeline pipeline = CreatePipeline(1);
  std::string markdown;
  for (int i = 0; i < 300; ++i) {
    markdown += "```mermaid\ngraph TD; A" + std::to_string(i) + "-->B" +
                std::to_string(i) + ";\n```\n\n";
  }
  const std::shared_ptr<MdRenderJob> job =
      pipeline.RenderAsync(markdown, MdRenderOptions{});
  job->Cancel();
  const MdHtmlResult result = job->Wait();
  EXPECT_TRUE(result.cancelled);
  EXPECT_FALSE(result.ok);
}

TEST(MdPipelineTest, ConcurrentJobsStayIndependent) {
  MdHtmlPipeline pipeline = CreatePipeline(2);
  const std::shared_ptr<MdRenderJob> first = pipeline.RenderAsync(
      "# One\n\n```cpp\nint a = 1;\n```\n", MdRenderOptions{});
  const std::shared_ptr<MdRenderJob> second = pipeline.RenderAsync(
      "# Two\n\n```python\ndef f():\n    pass\n```\n", MdRenderOptions{});
  const MdHtmlResult first_result = first->Wait();
  const MdHtmlResult second_result = second->Wait();
  ASSERT_TRUE(first_result.ok) << first_result.error;
  ASSERT_TRUE(second_result.ok) << second_result.error;
  EXPECT_NE(first_result.html.find("One"), std::string::npos);
  EXPECT_NE(second_result.html.find("Two"), std::string::npos);
  EXPECT_EQ(second_result.html.find("One"), std::string::npos);
}

TEST(MdPipelineTest, ServiceStatusIsIdleAfterRender) {
  MdHtmlPipeline pipeline = CreatePipeline(1);
  ASSERT_TRUE(pipeline.Render("# T\n", MdRenderOptions{}).ok);
  const MdServiceStatus status = pipeline.Status();
  EXPECT_EQ(status.active_jobs, 0);
  EXPECT_EQ(status.queued_tasks, 0);
  EXPECT_EQ(status.running_tasks, 0);
}

}  // namespace
