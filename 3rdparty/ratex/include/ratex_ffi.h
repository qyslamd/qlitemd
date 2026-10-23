#ifndef FILE_RATEX_FFI_H_
#define FILE_RATEX_FFI_H_

#include <stddef.h>

// RaTeX 的 C ABI。上游 0.1.15 只有 display list JSON，SVG 导出是本地补丁
// 增加的（见 patches/ratex/0001-add-svg-ffi.patch）。结构体布局必须与
// Rust 侧逐字段一致；新增字段一律追加在末尾，并用 struct_size 做能力发现。
#ifdef __cplusplus
extern "C" {
#endif

typedef struct RatexColor {
  float r;
  float g;
  float b;
  float a;
} RatexColor;

typedef struct RatexOptions {
  // 调用方必须填 sizeof(RatexOptions)；小于本结构体时只读已覆盖的字段
  size_t struct_size;
  // 0 = inline（text style），1 = display（display style）
  int display_mode;
  // NULL = 默认黑色
  const RatexColor* color;
  // 以下字段由本地补丁追加；struct_size 未覆盖时用默认值
  double font_size;     // 用户单位/em，默认 40
  double padding;       // 四周留白，默认 4
  double stroke_width;  // 未填充路径的线宽，默认 1.5
} RatexOptions;

typedef struct RatexResult {
  // 成功时是以 NUL 结尾的 UTF-8 SVG；用 ratex_free_display_list 释放
  char* data;
  // 0 = 成功
  int error_code;
} RatexResult;

// 解析 TeX 并直接渲染为独立 SVG（字形已转成 <path>，颜色烘进 fill）。
// 失败返回 error_code != 0，错误文本见 ratex_get_last_error（线程局部）。
RatexResult ratex_render_svg(const char* latex, const RatexOptions* opts);

// 释放 ratex_render_svg / ratex_parse_and_layout 返回的字符串
void ratex_free_display_list(char* ptr);

// 最近一次失败的原因；线程局部，返回的指针在下次调用前有效
const char* ratex_get_last_error(void);

#ifdef __cplusplus
}
#endif

#endif  // FILE_RATEX_FFI_H_
