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
