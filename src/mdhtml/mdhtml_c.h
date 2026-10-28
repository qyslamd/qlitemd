#ifndef FILE_MDHTML_MDHTML_C_H_
#define FILE_MDHTML_MDHTML_C_H_

#include <stddef.h>
#include <stdint.h>

// C ABI for the Qt-free core, intended for non-C++ hosts (C#, Rust, ...).
// Strings are UTF-8 and NUL-terminated; pointers returned by
// mdhtml_result_* stay valid until mdhtml_result_destroy.
//
// Build/link notes:
// - The shared library is qlitemd_mdhtml_c (DLL on Windows, .so on Unix).
//   Define MDHTML_SHARED for consumers of the shared library; the headers
//   then use dllimport/visibility attributes. Static C++ consumers of the
//   core do not need this file.
// - Functions and callbacks use the C calling convention (__cdecl on
//   Windows x86). C# should declare CallingConvention.Cdecl.
// - A progress callback runs on the render coordinator thread, never on a
//   UI thread. The user_data pointer must outlive the job.
// - mdhtml_render is a blocking convenience wrapper; mdhtml_render_async
//   exposes a job handle for GUI hosts.

#define MDHTML_ABI_VERSION 2u

#if defined(_WIN32)
#if defined(MDHTML_SHARED)
#if defined(MDHTML_BUILD)
#define MDHTML_API __declspec(dllexport)
#else
#define MDHTML_API __declspec(dllimport)
#endif
#else
#define MDHTML_API
#endif
#define MDHTML_CALL __cdecl
#else
#if defined(MDHTML_SHARED)
#define MDHTML_API __attribute__((visibility("default")))
#else
#define MDHTML_API
#endif
#define MDHTML_CALL
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct mdhtml_pipeline mdhtml_pipeline;
typedef struct mdhtml_result mdhtml_result;
typedef struct mdhtml_job mdhtml_job;

typedef int(MDHTML_CALL* mdhtml_resolve_family_fn)(void* user_data,
                                                   const char* css_list,
                                                   char* buffer,
                                                   size_t buffer_size);
typedef int(MDHTML_CALL* mdhtml_measure_text_fn)(
    void* user_data, const char* text, const char* family, double size_px,
    int weight, int italic, double* width, double* ascent, double* descent);
typedef const char*(MDHTML_CALL* mdhtml_fingerprint_fn)(void* user_data);
typedef void(MDHTML_CALL* mdhtml_progress_fn)(void* user_data, int completed,
                                              int total, int in_flight);

typedef struct mdhtml_font_metrics {
  uint32_t struct_size;
  void* user_data;
  // Writes a NUL-terminated family name into buffer. Return 1 on success,
  // 0 to fall back to the built-in approximation.
  mdhtml_resolve_family_fn resolve_family;
  // Returns 1 and fills width/ascent/descent on success. Callbacks must be
  // thread-safe: several render workers may call them concurrently.
  mdhtml_measure_text_fn measure_text;
  // Optional stable identity for cache keys; may be NULL.
  mdhtml_fingerprint_fn fingerprint;
} mdhtml_font_metrics;

#define MDHTML_ADMONITION_GITHUB 0
#define MDHTML_ADMONITION_MPE 1

typedef struct mdhtml_render_options {
  uint32_t struct_size;
  int theme;                    // 0 = light, 1 = dark
  uint32_t formula_color_rgba;  // 0xRRGGBBAA; 0 = opaque black
  int formula_enabled;
  int mermaid_enabled;
  int code_line_numbers;
  // Appended for ABI-compatible struct_size growth. Default is MPE.
  int admonition_mode;  // MDHTML_ADMONITION_GITHUB or _MPE
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

MDHTML_API uint32_t MDHTML_CALL mdhtml_abi_version(void);
// ABI version plus the library version string ("x.y.z").
MDHTML_API const char* MDHTML_CALL mdhtml_version_string(void);

MDHTML_API void MDHTML_CALL
mdhtml_font_metrics_init(mdhtml_font_metrics* metrics);
MDHTML_API void MDHTML_CALL
mdhtml_render_options_init(mdhtml_render_options* options);

MDHTML_API mdhtml_pipeline* MDHTML_CALL mdhtml_pipeline_create(void);
MDHTML_API mdhtml_pipeline* MDHTML_CALL
mdhtml_pipeline_create_with_metrics(const mdhtml_font_metrics* metrics);
MDHTML_API void MDHTML_CALL mdhtml_pipeline_destroy(mdhtml_pipeline* pipeline);
// Cancels every active job owned by this pipeline.
MDHTML_API void MDHTML_CALL
mdhtml_pipeline_cancel_all(mdhtml_pipeline* pipeline);

// Blocking render. Returns NULL only on allocation failure; parse/render
// errors are reported through mdhtml_result_ok/error.
MDHTML_API mdhtml_result* MDHTML_CALL
mdhtml_render(mdhtml_pipeline* pipeline, const char* markdown,
              const mdhtml_render_options* options);

// Asynchronous render. Returns NULL on allocation failure or invalid
// pipeline. progress/user_data may be NULL; user_data must outlive the job.
MDHTML_API mdhtml_job* MDHTML_CALL
mdhtml_render_async(mdhtml_pipeline* pipeline, const char* markdown,
                    const mdhtml_render_options* options,
                    mdhtml_progress_fn progress, void* user_data);
MDHTML_API void MDHTML_CALL mdhtml_job_cancel(mdhtml_job* job);
MDHTML_API int MDHTML_CALL mdhtml_job_is_finished(const mdhtml_job* job);
// Blocks until the job finishes and returns a new result handle. The caller
// owns the result. Safe to call more than once; do not call concurrently
// with mdhtml_job_destroy.
MDHTML_API mdhtml_result* MDHTML_CALL mdhtml_job_wait(mdhtml_job* job);
// Cancels a running job, waits for its coordinator to finish, and frees it.
MDHTML_API void MDHTML_CALL mdhtml_job_destroy(mdhtml_job* job);

MDHTML_API void MDHTML_CALL mdhtml_result_destroy(mdhtml_result* result);

MDHTML_API int MDHTML_CALL mdhtml_result_ok(const mdhtml_result* result);
MDHTML_API int MDHTML_CALL mdhtml_result_cancelled(const mdhtml_result* result);
MDHTML_API const char* MDHTML_CALL
mdhtml_result_error(const mdhtml_result* result);
MDHTML_API const char* MDHTML_CALL
mdhtml_result_html(const mdhtml_result* result);

MDHTML_API size_t MDHTML_CALL
mdhtml_result_asset_count(const mdhtml_result* result);
MDHTML_API const char* MDHTML_CALL
mdhtml_result_asset_id(const mdhtml_result* result, size_t index);
MDHTML_API int MDHTML_CALL mdhtml_result_asset_kind(const mdhtml_result* result,
                                                    size_t index);
MDHTML_API const char* MDHTML_CALL
mdhtml_result_asset_source(const mdhtml_result* result, size_t index);
MDHTML_API const char* MDHTML_CALL
mdhtml_result_asset_svg(const mdhtml_result* result, size_t index);
MDHTML_API int MDHTML_CALL
mdhtml_result_asset_width(const mdhtml_result* result, size_t index);
MDHTML_API int MDHTML_CALL
mdhtml_result_asset_height(const mdhtml_result* result, size_t index);
MDHTML_API int MDHTML_CALL
mdhtml_result_asset_display(const mdhtml_result* result, size_t index);

MDHTML_API size_t MDHTML_CALL
mdhtml_result_outline_count(const mdhtml_result* result);
MDHTML_API int MDHTML_CALL
mdhtml_result_outline_level(const mdhtml_result* result, size_t index);
MDHTML_API const char* MDHTML_CALL
mdhtml_result_outline_text(const mdhtml_result* result, size_t index);
MDHTML_API const char* MDHTML_CALL
mdhtml_result_outline_anchor(const mdhtml_result* result, size_t index);
// ASCII element id ("h-0"), suitable for anchor scrolling.
MDHTML_API const char* MDHTML_CALL
mdhtml_result_outline_id(const mdhtml_result* result, size_t index);

// Code blocks in source order; id matches the copy:<id> link in HTML.
MDHTML_API size_t MDHTML_CALL
mdhtml_result_code_block_count(const mdhtml_result* result);
MDHTML_API const char* MDHTML_CALL
mdhtml_result_code_block_id(const mdhtml_result* result, size_t index);
MDHTML_API const char* MDHTML_CALL
mdhtml_result_code_block_language(const mdhtml_result* result, size_t index);
MDHTML_API const char* MDHTML_CALL
mdhtml_result_code_block_code(const mdhtml_result* result, size_t index);

// Copies only the fields covered by stats->struct_size.
MDHTML_API int MDHTML_CALL mdhtml_result_stats(const mdhtml_result* result,
                                               mdhtml_stats* stats);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // FILE_MDHTML_MDHTML_C_H_
