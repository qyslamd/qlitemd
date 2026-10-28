#include "mdhtml_c.h"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <string>

#include <gtest/gtest.h>

namespace {

struct MetricCounters {
  std::atomic<int> resolve{0};
  std::atomic<int> measure{0};
};

int ResolveFamilyCallback(void* user_data, const char* css_list, char* buffer,
                          size_t buffer_size) {
  (void)css_list;
  auto* counters = static_cast<MetricCounters*>(user_data);
  ++counters->resolve;
  std::snprintf(buffer, buffer_size, "%s", "C Test Sans");
  return 1;
}

int MeasureCallback(void* user_data, const char* text, const char* family,
                    double size_px, int weight, int italic, double* width,
                    double* ascent, double* descent) {
  (void)family;
  (void)weight;
  (void)italic;
  auto* counters = static_cast<MetricCounters*>(user_data);
  ++counters->measure;
  *width = std::strlen(text) * size_px * 0.5;
  *ascent = size_px * 0.8;
  *descent = size_px * 0.2;
  return 1;
}

const char* FingerprintCallback(void* user_data) {
  (void)user_data;
  return "c-metrics-v1";
}

struct ProgressState {
  std::atomic<int> calls{0};
  std::atomic<int> last_completed{0};
  std::atomic<int> total{0};
};

void ProgressCallback(void* user_data, int completed, int total,
                      int in_flight) {
  (void)in_flight;
  auto* state = static_cast<ProgressState*>(user_data);
  ++state->calls;
  state->last_completed.store(completed);
  state->total.store(total);
}

TEST(MdhtmlCApiTest, RendersDocumentAssetsOutlineAndStats) {
  mdhtml_pipeline* pipeline = mdhtml_pipeline_create();
  ASSERT_NE(pipeline, nullptr);

  mdhtml_render_options options;
  mdhtml_render_options_init(&options);
  mdhtml_result* result = mdhtml_render(pipeline,
                                        "# Heading\n\nInline $x^2$.\n\n"
                                        "```mermaid\ngraph TD; A-->B;\n```\n",
                                        &options);
  ASSERT_NE(result, nullptr);
  ASSERT_EQ(mdhtml_result_ok(result), 1) << mdhtml_result_error(result);
  EXPECT_EQ(mdhtml_result_cancelled(result), 0);

  const std::string html = mdhtml_result_html(result);
  EXPECT_NE(html.find("qlitemd-asset:"), std::string::npos);
  EXPECT_NE(html.find("class=\"mermaid\""), std::string::npos);

  const size_t asset_count = mdhtml_result_asset_count(result);
  ASSERT_GE(asset_count, 2u);
  for (size_t i = 0; i < asset_count; ++i) {
    EXPECT_NE(mdhtml_result_asset_id(result, i), nullptr);
    EXPECT_NE(mdhtml_result_asset_source(result, i), nullptr);
    EXPECT_NE(mdhtml_result_asset_svg(result, i), nullptr);
    EXPECT_GT(mdhtml_result_asset_width(result, i), 0);
    EXPECT_GT(mdhtml_result_asset_height(result, i), 0);
    const int kind = mdhtml_result_asset_kind(result, i);
    EXPECT_TRUE(kind == 0 || kind == 1);
  }
  EXPECT_EQ(mdhtml_result_asset_kind(result, asset_count), -1);
  EXPECT_EQ(mdhtml_result_asset_id(result, asset_count), nullptr);

  ASSERT_EQ(mdhtml_result_outline_count(result), 1u);
  EXPECT_EQ(mdhtml_result_outline_level(result, 0), 1);
  EXPECT_STREQ(mdhtml_result_outline_text(result, 0), "Heading");
  EXPECT_STREQ(mdhtml_result_outline_anchor(result, 0), "heading");
  EXPECT_STREQ(mdhtml_result_outline_id(result, 0), "h-0");

  mdhtml_stats stats;
  std::memset(&stats, 0, sizeof(stats));
  stats.struct_size = sizeof(stats);
  ASSERT_EQ(mdhtml_result_stats(result, &stats), 1);
  EXPECT_EQ(stats.headings, 1);
  EXPECT_EQ(stats.asset_count, static_cast<int>(asset_count));
  EXPECT_GE(stats.worker_tasks, 3);

  mdhtml_result_destroy(result);
  mdhtml_pipeline_destroy(pipeline);
}

TEST(MdhtmlCApiTest, ReportsAbiAndLibraryVersion) {
  EXPECT_EQ(mdhtml_abi_version(), MDHTML_ABI_VERSION);
  const char* version = mdhtml_version_string();
  ASSERT_NE(version, nullptr);
  EXPECT_NE(std::string(version).find('.'), std::string::npos);
}

TEST(MdhtmlCApiTest, CodeBlockAccessorsMatchCopyLinks) {
  mdhtml_pipeline* pipeline = mdhtml_pipeline_create();
  ASSERT_NE(pipeline, nullptr);
  mdhtml_result* result =
      mdhtml_render(pipeline, "```cpp\nint x = 1;\n```\n", nullptr);
  ASSERT_NE(result, nullptr);
  ASSERT_EQ(mdhtml_result_ok(result), 1) << mdhtml_result_error(result);
  ASSERT_EQ(mdhtml_result_code_block_count(result), 1u);
  EXPECT_STREQ(mdhtml_result_code_block_id(result, 0), "0");
  EXPECT_STREQ(mdhtml_result_code_block_language(result, 0), "cpp");
  EXPECT_STREQ(mdhtml_result_code_block_code(result, 0), "int x = 1;\n");
  EXPECT_EQ(mdhtml_result_code_block_id(result, 1), nullptr);
  EXPECT_NE(std::string(mdhtml_result_html(result)).find("href=\"copy:0\""),
            std::string::npos);
  mdhtml_result_destroy(result);
  mdhtml_pipeline_destroy(pipeline);
}

TEST(MdhtmlCApiTest, AsyncRenderReportsProgress) {
  mdhtml_pipeline* pipeline = mdhtml_pipeline_create();
  ASSERT_NE(pipeline, nullptr);
  ProgressState progress;
  mdhtml_job* job = mdhtml_render_async(pipeline,
                                        "# Async\n\n"
                                        "Inline $x^2$.\n\n"
                                        "```cpp\n"
                                        "int a = 1;\n"
                                        "```\n",
                                        nullptr, &ProgressCallback, &progress);
  ASSERT_NE(job, nullptr);
  mdhtml_result* result = mdhtml_job_wait(job);
  ASSERT_NE(result, nullptr);
  ASSERT_EQ(mdhtml_result_ok(result), 1) << mdhtml_result_error(result);
  EXPECT_GE(progress.calls.load(), 1);
  EXPECT_EQ(progress.total.load(), progress.last_completed.load());
  EXPECT_EQ(mdhtml_job_is_finished(job), 1);
  mdhtml_result_destroy(result);
  mdhtml_job_destroy(job);
  mdhtml_pipeline_destroy(pipeline);
}

TEST(MdhtmlCApiTest, AsyncCancelReturnsCancelledResult) {
  mdhtml_pipeline* pipeline = mdhtml_pipeline_create();
  ASSERT_NE(pipeline, nullptr);
  std::string markdown;
  for (int i = 0; i < 300; ++i) {
    markdown += "```mermaid\ngraph TD; A" + std::to_string(i) + "-->B" +
                std::to_string(i) + ";\n```\n\n";
  }
  mdhtml_job* job = mdhtml_render_async(pipeline, markdown.c_str(), nullptr,
                                        nullptr, nullptr);
  ASSERT_NE(job, nullptr);
  mdhtml_job_cancel(job);
  mdhtml_result* result = mdhtml_job_wait(job);
  ASSERT_NE(result, nullptr);
  EXPECT_EQ(mdhtml_result_cancelled(result), 1);
  EXPECT_EQ(mdhtml_result_ok(result), 0);
  mdhtml_result_destroy(result);
  mdhtml_job_destroy(job);
  mdhtml_pipeline_destroy(pipeline);
}

TEST(MdhtmlCApiTest, AdmonitionModeDefaultsToMpe) {
  mdhtml_pipeline* pipeline = mdhtml_pipeline_create();
  ASSERT_NE(pipeline, nullptr);
  mdhtml_render_options options;
  mdhtml_render_options_init(&options);
  EXPECT_EQ(options.admonition_mode, MDHTML_ADMONITION_MPE);

  mdhtml_result* mpe =
      mdhtml_render(pipeline, "> [!info] Inline title\n", &options);
  ASSERT_NE(mpe, nullptr);
  ASSERT_EQ(mdhtml_result_ok(mpe), 1) << mdhtml_result_error(mpe);
  const std::string mpe_html = mdhtml_result_html(mpe);
  EXPECT_NE(mpe_html.find("callout-info"), std::string::npos);
  EXPECT_NE(mpe_html.find("Inline title"), std::string::npos);
  mdhtml_result_destroy(mpe);

  options.admonition_mode = MDHTML_ADMONITION_GITHUB;
  mdhtml_result* github =
      mdhtml_render(pipeline, "> [!IMPORTANT]\n> Body.\n", &options);
  ASSERT_NE(github, nullptr);
  ASSERT_EQ(mdhtml_result_ok(github), 1) << mdhtml_result_error(github);
  const std::string github_html = mdhtml_result_html(github);
  EXPECT_NE(github_html.find("admonition-important"), std::string::npos);
  EXPECT_EQ(github_html.find("callout-important"), std::string::npos);
  mdhtml_result_destroy(github);

  mdhtml_pipeline_destroy(pipeline);
}

TEST(MdhtmlCApiTest, CustomMetricsCallbacksAreUsed) {
  MetricCounters counters;
  mdhtml_font_metrics metrics;
  mdhtml_font_metrics_init(&metrics);
  metrics.user_data = &counters;
  metrics.resolve_family = &ResolveFamilyCallback;
  metrics.measure_text = &MeasureCallback;
  metrics.fingerprint = &FingerprintCallback;

  mdhtml_pipeline* pipeline = mdhtml_pipeline_create_with_metrics(&metrics);
  ASSERT_NE(pipeline, nullptr);
  mdhtml_render_options options;
  mdhtml_render_options_init(&options);
  mdhtml_result* result = mdhtml_render(
      pipeline, "```mermaid\ngraph TD; A[Hello]-->B[World];\n```\n", &options);
  ASSERT_NE(result, nullptr);
  ASSERT_EQ(mdhtml_result_ok(result), 1) << mdhtml_result_error(result);
  EXPECT_GT(counters.resolve.load(), 0);
  EXPECT_GT(counters.measure.load(), 0);

  mdhtml_result_destroy(result);
  mdhtml_pipeline_destroy(pipeline);
}

TEST(MdhtmlCApiTest, NullInputsAreSafe) {
  EXPECT_EQ(mdhtml_render(nullptr, "x", nullptr), nullptr);
  mdhtml_result_destroy(nullptr);
  mdhtml_pipeline_destroy(nullptr);
  mdhtml_pipeline_cancel_all(nullptr);
  mdhtml_job_destroy(nullptr);
  mdhtml_job_cancel(nullptr);
  EXPECT_EQ(mdhtml_job_wait(nullptr), nullptr);
  EXPECT_EQ(mdhtml_job_is_finished(nullptr), 1);
  EXPECT_EQ(mdhtml_result_ok(nullptr), 0);
  EXPECT_STREQ(mdhtml_result_html(nullptr), "");
  EXPECT_EQ(mdhtml_result_asset_count(nullptr), 0u);
  EXPECT_EQ(mdhtml_result_outline_count(nullptr), 0u);

  mdhtml_pipeline* pipeline = mdhtml_pipeline_create();
  ASSERT_NE(pipeline, nullptr);
  mdhtml_result* result = mdhtml_render(pipeline, nullptr, nullptr);
  ASSERT_NE(result, nullptr);
  EXPECT_EQ(mdhtml_result_ok(result), 1);
  mdhtml_result_destroy(result);
  mdhtml_pipeline_destroy(pipeline);
}

}  // namespace
