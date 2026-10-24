#ifndef FILE_MDHTML_MDHTML_C_H_
#define FILE_MDHTML_MDHTML_C_H_

#include <stddef.h>
#include <stdint.h>

// C ABI for the Qt-free core. Strings are UTF-8 and NUL-terminated; pointers
// returned by mdhtml_result_* stay valid until mdhtml_result_destroy.
//
// The first API is synchronous: mdhtml_render blocks until the worker lanes
// finish. An asynchronous handle will follow for GUI consumers.

#ifdef __cplusplus
extern "C" {
#endif

typedef struct mdhtml_pipeline mdhtml_pipeline;
typedef struct mdhtml_result mdhtml_result;

typedef struct mdhtml_font_metrics {
  uint32_t struct_size;
  void* user_data;
  // Writes a NUL-terminated family name into buffer. Return 1 on success,
  // 0 to fall back to the built-in approximation.
  int (*resolve_family)(void* user_data, const char* css_list, char* buffer,
                        size_t buffer_size);
  // Returns 1 and fills width/ascent/descent on success. Callbacks must be
  // thread-safe: several render workers may call them concurrently.
  int (*measure_text)(void* user_data, const char* text, const char* family,
                      double size_px, int weight, int italic, double* width,
                      double* ascent, double* descent);
  // Optional stable identity for cache keys; may be NULL.
  const char* (*fingerprint)(void* user_data);
} mdhtml_font_metrics;

typedef struct mdhtml_render_options {
  uint32_t struct_size;
  int theme;                    // 0 = light, 1 = dark
  uint32_t formula_color_rgba;  // 0xRRGGBBAA; 0 = opaque black
  int formula_enabled;
  int mermaid_enabled;
  int code_line_numbers;
} mdhtml_render_options;

typedef struct mdhtml_stats {
  uint32_t struct_size;
  int source_chars;
  int html_chars;
  int headings;
  int code_blocks;
  int formulas;
  int mermaid_blocks;
  int asset_count;
  int worker_tasks;
  int worker_failures;
  int64_t parse_ms;
  int64_t collect_ms;
  int64_t worker_ms;
  int64_t assemble_ms;
} mdhtml_stats;

void mdhtml_font_metrics_init(mdhtml_font_metrics* metrics);
void mdhtml_render_options_init(mdhtml_render_options* options);

mdhtml_pipeline* mdhtml_pipeline_create(void);
mdhtml_pipeline* mdhtml_pipeline_create_with_metrics(
    const mdhtml_font_metrics* metrics);
void mdhtml_pipeline_destroy(mdhtml_pipeline* pipeline);
void mdhtml_pipeline_cancel_all(mdhtml_pipeline* pipeline);

// Synchronous render; returns NULL only when allocation fails. A NULL
// markdown is treated as empty input.
mdhtml_result* mdhtml_render(mdhtml_pipeline* pipeline, const char* markdown,
                             const mdhtml_render_options* options);
void mdhtml_result_destroy(mdhtml_result* result);

int mdhtml_result_ok(const mdhtml_result* result);
int mdhtml_result_cancelled(const mdhtml_result* result);
const char* mdhtml_result_error(const mdhtml_result* result);
const char* mdhtml_result_html(const mdhtml_result* result);

size_t mdhtml_result_asset_count(const mdhtml_result* result);
const char* mdhtml_result_asset_id(const mdhtml_result* result, size_t index);
int mdhtml_result_asset_kind(const mdhtml_result* result, size_t index);
const char* mdhtml_result_asset_source(const mdhtml_result* result,
                                       size_t index);
const char* mdhtml_result_asset_svg(const mdhtml_result* result, size_t index);
int mdhtml_result_asset_width(const mdhtml_result* result, size_t index);
int mdhtml_result_asset_height(const mdhtml_result* result, size_t index);
int mdhtml_result_asset_display(const mdhtml_result* result, size_t index);

size_t mdhtml_result_outline_count(const mdhtml_result* result);
int mdhtml_result_outline_level(const mdhtml_result* result, size_t index);
const char* mdhtml_result_outline_text(const mdhtml_result* result,
                                       size_t index);
const char* mdhtml_result_outline_anchor(const mdhtml_result* result,
                                         size_t index);

// Copies only the fields covered by stats->struct_size.
int mdhtml_result_stats(const mdhtml_result* result, mdhtml_stats* stats);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // FILE_MDHTML_MDHTML_C_H_
