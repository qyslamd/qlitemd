#include "mdhtml_c.h"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <string_view>
#include <utility>

#include "md_font_metrics.h"
#include "md_pipeline.h"

#ifndef MDHTML_VERSION_STRING
#define MDHTML_VERSION_STRING "0.0.0"
#endif

namespace {

bool Covers(const mdhtml_font_metrics& metrics, std::size_t offset,
            std::size_t size) {
  return metrics.struct_size >= offset + size;
}

bool Covers(const mdhtml_render_options& options, std::size_t offset,
            std::size_t size) {
  return options.struct_size >= offset + size;
}

class MdCFontMetrics final : public MdFontMetrics {
 public:
  explicit MdCFontMetrics(mdhtml_font_metrics callbacks)
      : callbacks_(callbacks) {}

  std::string ResolveFamily(std::string_view css_list) const override {
    if (Covers(callbacks_, offsetof(mdhtml_font_metrics, resolve_family),
               sizeof(callbacks_.resolve_family)) &&
        callbacks_.resolve_family != nullptr) {
      const std::string css(css_list);
      char buffer[256] = {};
      if (callbacks_.resolve_family(callbacks_.user_data, css.c_str(), buffer,
                                    sizeof(buffer)) != 0) {
        buffer[sizeof(buffer) - 1] = '\0';
        if (buffer[0] != '\0') {
          return buffer;
        }
      }
    }
    return fallback_.ResolveFamily(css_list);
  }

  MdTextMetrics Measure(std::string_view text,
                        const MdFontSpec& font) const override {
    if (Covers(callbacks_, offsetof(mdhtml_font_metrics, measure_text),
               sizeof(callbacks_.measure_text)) &&
        callbacks_.measure_text != nullptr) {
      const std::string owned_text(text);
      const std::string family(font.family);
      double width = 0.0;
      double ascent = 0.0;
      double descent = 0.0;
      if (callbacks_.measure_text(callbacks_.user_data, owned_text.c_str(),
                                  family.c_str(), font.size_px, font.weight,
                                  font.italic ? 1 : 0, &width, &ascent,
                                  &descent) != 0) {
        MdTextMetrics metrics;
        metrics.width = width;
        metrics.ascent = ascent;
        metrics.descent = descent;
        return metrics;
      }
    }
    return fallback_.Measure(text, font);
  }

  std::string Fingerprint() const override {
    if (Covers(callbacks_, offsetof(mdhtml_font_metrics, fingerprint),
               sizeof(callbacks_.fingerprint)) &&
        callbacks_.fingerprint != nullptr) {
      const char* fingerprint = callbacks_.fingerprint(callbacks_.user_data);
      if (fingerprint != nullptr && fingerprint[0] != '\0') {
        return fingerprint;
      }
    }
    return fallback_.Fingerprint();
  }

 private:
  mdhtml_font_metrics callbacks_;
  MdDefaultFontMetrics fallback_;
};

MdRenderOptions ToRenderOptions(const mdhtml_render_options* options) {
  MdRenderOptions out;
  if (options == nullptr || options->struct_size == 0) {
    return out;
  }
  out.theme = options->theme == 1 ? MdTheme::Dark : MdTheme::Light;
  if (options->formula_color_rgba != 0) {
    out.formula_color.r =
        static_cast<uint8_t>((options->formula_color_rgba >> 24) & 0xFF);
    out.formula_color.g =
        static_cast<uint8_t>((options->formula_color_rgba >> 16) & 0xFF);
    out.formula_color.b =
        static_cast<uint8_t>((options->formula_color_rgba >> 8) & 0xFF);
    out.formula_color.a =
        static_cast<uint8_t>(options->formula_color_rgba & 0xFF);
  }
  out.formula = options->formula_enabled != 0;
  out.mermaid = options->mermaid_enabled != 0;
  out.code_line_numbers = options->code_line_numbers != 0;
  if (Covers(*options, offsetof(mdhtml_render_options, admonition_mode),
             sizeof(options->admonition_mode))) {
    out.admonition_mode = options->admonition_mode == MDHTML_ADMONITION_GITHUB
                              ? MdAdmonitionMode::kGithub
                              : MdAdmonitionMode::kMpe;
  }
  return out;
}

}  // namespace

struct mdhtml_pipeline {
  explicit mdhtml_pipeline(std::shared_ptr<const MdFontMetrics> metrics) {
    MdPipelineOptions options;
    options.font_metrics = std::move(metrics);
    pipeline = std::make_unique<MdHtmlPipeline>(options);
  }
  std::unique_ptr<MdHtmlPipeline> pipeline;
};

struct mdhtml_result {
  MdHtmlResult result;
};

struct mdhtml_job {
  std::shared_ptr<MdRenderJob> job;
  std::mutex wait_mutex;
};

namespace {

const MdAsset* AssetAt(const mdhtml_result* result, std::size_t index) {
  if (result == nullptr || index >= result->result.assets.size()) {
    return nullptr;
  }
  return &result->result.assets[index];
}

const MdHeading* HeadingAt(const mdhtml_result* result, std::size_t index) {
  if (result == nullptr || index >= result->result.outline.size()) {
    return nullptr;
  }
  return &result->result.outline[index];
}

const MdCodeBlock* CodeBlockAt(const mdhtml_result* result, std::size_t index) {
  if (result == nullptr || index >= result->result.code_blocks.size()) {
    return nullptr;
  }
  return &result->result.code_blocks[index];
}

}  // namespace

extern "C" {

uint32_t mdhtml_abi_version(void) { return MDHTML_ABI_VERSION; }

const char* mdhtml_version_string(void) { return MDHTML_VERSION_STRING; }

void mdhtml_font_metrics_init(mdhtml_font_metrics* metrics) {
  if (metrics == nullptr) {
    return;
  }
  std::memset(metrics, 0, sizeof(*metrics));
  metrics->struct_size = sizeof(*metrics);
}

void mdhtml_render_options_init(mdhtml_render_options* options) {
  if (options == nullptr) {
    return;
  }
  std::memset(options, 0, sizeof(*options));
  options->struct_size = sizeof(*options);
  options->theme = 0;
  options->formula_color_rgba = 0x000000FFu;
  options->formula_enabled = 1;
  options->mermaid_enabled = 1;
  options->code_line_numbers = 0;
  options->admonition_mode = MDHTML_ADMONITION_MPE;
}

mdhtml_pipeline* mdhtml_pipeline_create(void) {
  try {
    return new mdhtml_pipeline(nullptr);
  } catch (...) {
    return nullptr;
  }
}

mdhtml_pipeline* mdhtml_pipeline_create_with_metrics(
    const mdhtml_font_metrics* metrics) {
  try {
    if (metrics == nullptr || metrics->struct_size == 0) {
      return mdhtml_pipeline_create();
    }
    auto adapter = std::make_shared<MdCFontMetrics>(*metrics);
    return new mdhtml_pipeline(std::move(adapter));
  } catch (...) {
    return nullptr;
  }
}

void mdhtml_pipeline_destroy(mdhtml_pipeline* pipeline) { delete pipeline; }

void mdhtml_pipeline_cancel_all(mdhtml_pipeline* pipeline) {
  if (pipeline != nullptr && pipeline->pipeline) {
    pipeline->pipeline->CancelAll();
  }
}

mdhtml_job* mdhtml_render_async(mdhtml_pipeline* pipeline, const char* markdown,
                                const mdhtml_render_options* options,
                                mdhtml_progress_fn progress, void* user_data) {
  if (pipeline == nullptr || !pipeline->pipeline) {
    return nullptr;
  }
  auto handle = std::unique_ptr<mdhtml_job>(new (std::nothrow) mdhtml_job);
  if (!handle) {
    return nullptr;
  }
  try {
    MdProgressCallback callback;
    if (progress != nullptr) {
      callback = [progress, user_data](int completed, int total,
                                       int in_flight) {
        progress(user_data, completed, total, in_flight);
      };
    }
    handle->job = pipeline->pipeline->RenderAsync(
        markdown != nullptr ? markdown : "", ToRenderOptions(options),
        std::move(callback));
  } catch (const std::bad_alloc&) {
    return nullptr;
  } catch (...) {
    return nullptr;
  }
  if (!handle->job) {
    return nullptr;
  }
  return handle.release();
}

void mdhtml_job_cancel(mdhtml_job* job) {
  if (job != nullptr && job->job) {
    job->job->Cancel();
  }
}

int mdhtml_job_is_finished(const mdhtml_job* job) {
  return job == nullptr || !job->job || job->job->IsFinished() ? 1 : 0;
}

mdhtml_result* mdhtml_job_wait(mdhtml_job* job) {
  if (job == nullptr || !job->job) {
    return nullptr;
  }
  std::lock_guard<std::mutex> lock(job->wait_mutex);
  auto* output = new (std::nothrow) mdhtml_result;
  if (output == nullptr) {
    return nullptr;
  }
  try {
    output->result = job->job->Wait();
  } catch (const std::bad_alloc&) {
    delete output;
    return nullptr;
  } catch (...) {
    output->result = MdHtmlResult{};
    output->result.error = "render failed";
  }
  return output;
}

void mdhtml_job_destroy(mdhtml_job* job) {
  if (job == nullptr) {
    return;
  }
  if (job->job) {
    job->job->Cancel();
    job->job->Wait();
  }
  delete job;
}

mdhtml_result* mdhtml_render(mdhtml_pipeline* pipeline, const char* markdown,
                             const mdhtml_render_options* options) {
  mdhtml_job* job =
      mdhtml_render_async(pipeline, markdown, options, nullptr, nullptr);
  if (job == nullptr) {
    return nullptr;
  }
  mdhtml_result* result = mdhtml_job_wait(job);
  mdhtml_job_destroy(job);
  return result;
}

void mdhtml_result_destroy(mdhtml_result* result) { delete result; }

int mdhtml_result_ok(const mdhtml_result* result) {
  return result != nullptr && result->result.ok ? 1 : 0;
}

int mdhtml_result_cancelled(const mdhtml_result* result) {
  return result != nullptr && result->result.cancelled ? 1 : 0;
}

const char* mdhtml_result_error(const mdhtml_result* result) {
  return result != nullptr ? result->result.error.c_str() : "";
}

const char* mdhtml_result_html(const mdhtml_result* result) {
  return result != nullptr ? result->result.html.c_str() : "";
}

size_t mdhtml_result_asset_count(const mdhtml_result* result) {
  return result != nullptr ? result->result.assets.size() : 0;
}

const char* mdhtml_result_asset_id(const mdhtml_result* result, size_t index) {
  const MdAsset* asset = AssetAt(result, index);
  return asset != nullptr ? asset->id.c_str() : nullptr;
}

int mdhtml_result_asset_kind(const mdhtml_result* result, size_t index) {
  const MdAsset* asset = AssetAt(result, index);
  if (asset == nullptr) {
    return -1;
  }
  return asset->kind == MdAssetKind::Mermaid ? 1 : 0;
}

const char* mdhtml_result_asset_source(const mdhtml_result* result,
                                       size_t index) {
  const MdAsset* asset = AssetAt(result, index);
  return asset != nullptr ? asset->source.c_str() : nullptr;
}

const char* mdhtml_result_asset_svg(const mdhtml_result* result, size_t index) {
  const MdAsset* asset = AssetAt(result, index);
  return asset != nullptr ? asset->image.svg.c_str() : nullptr;
}

int mdhtml_result_asset_width(const mdhtml_result* result, size_t index) {
  const MdAsset* asset = AssetAt(result, index);
  return asset != nullptr ? asset->image.width : 0;
}

int mdhtml_result_asset_height(const mdhtml_result* result, size_t index) {
  const MdAsset* asset = AssetAt(result, index);
  return asset != nullptr ? asset->image.height : 0;
}

int mdhtml_result_asset_display(const mdhtml_result* result, size_t index) {
  const MdAsset* asset = AssetAt(result, index);
  return asset != nullptr && asset->display ? 1 : 0;
}

size_t mdhtml_result_outline_count(const mdhtml_result* result) {
  return result != nullptr ? result->result.outline.size() : 0;
}

int mdhtml_result_outline_level(const mdhtml_result* result, size_t index) {
  const MdHeading* heading = HeadingAt(result, index);
  return heading != nullptr ? heading->level : 0;
}

const char* mdhtml_result_outline_text(const mdhtml_result* result,
                                       size_t index) {
  const MdHeading* heading = HeadingAt(result, index);
  return heading != nullptr ? heading->text.c_str() : nullptr;
}

const char* mdhtml_result_outline_anchor(const mdhtml_result* result,
                                         size_t index) {
  const MdHeading* heading = HeadingAt(result, index);
  return heading != nullptr ? heading->anchor.c_str() : nullptr;
}

const char* mdhtml_result_outline_id(const mdhtml_result* result,
                                     size_t index) {
  const MdHeading* heading = HeadingAt(result, index);
  return heading != nullptr ? heading->id.c_str() : nullptr;
}

size_t mdhtml_result_code_block_count(const mdhtml_result* result) {
  return result != nullptr ? result->result.code_blocks.size() : 0;
}

const char* mdhtml_result_code_block_id(const mdhtml_result* result,
                                        size_t index) {
  const MdCodeBlock* block = CodeBlockAt(result, index);
  return block != nullptr ? block->id.c_str() : nullptr;
}

const char* mdhtml_result_code_block_language(const mdhtml_result* result,
                                              size_t index) {
  const MdCodeBlock* block = CodeBlockAt(result, index);
  return block != nullptr ? block->language.c_str() : nullptr;
}

const char* mdhtml_result_code_block_code(const mdhtml_result* result,
                                          size_t index) {
  const MdCodeBlock* block = CodeBlockAt(result, index);
  return block != nullptr ? block->code.c_str() : nullptr;
}

int mdhtml_result_stats(const mdhtml_result* result, mdhtml_stats* stats) {
  if (result == nullptr || stats == nullptr || stats->struct_size == 0) {
    return 0;
  }
  const MdRenderStats& source = result->result.stats;
  const std::size_t limit =
      std::min<std::size_t>(stats->struct_size, sizeof(mdhtml_stats));
  auto assign = [&](std::size_t offset, auto value) {
    if (offset + sizeof(value) <= limit) {
      std::memcpy(reinterpret_cast<char*>(stats) + offset, &value,
                  sizeof(value));
    }
  };
  assign(offsetof(mdhtml_stats, source_chars), source.source_chars);
  assign(offsetof(mdhtml_stats, html_chars), source.html_chars);
  assign(offsetof(mdhtml_stats, headings), source.headings);
  assign(offsetof(mdhtml_stats, code_blocks), source.code_blocks);
  assign(offsetof(mdhtml_stats, formulas), source.formulas);
  assign(offsetof(mdhtml_stats, mermaid_blocks), source.mermaid_blocks);
  assign(offsetof(mdhtml_stats, asset_count), source.asset_count);
  assign(offsetof(mdhtml_stats, worker_tasks), source.worker_tasks);
  assign(offsetof(mdhtml_stats, worker_failures), source.worker_failures);
  assign(offsetof(mdhtml_stats, parse_ms), source.parse_ms);
  assign(offsetof(mdhtml_stats, collect_ms), source.collect_ms);
  assign(offsetof(mdhtml_stats, worker_ms), source.worker_ms);
  assign(offsetof(mdhtml_stats, assemble_ms), source.assemble_ms);
  return 1;
}

}  // extern "C"
