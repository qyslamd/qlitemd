#ifndef FILE_MARKDOWN_MARKDOWN_BYTECODE_H_
#define FILE_MARKDOWN_MARKDOWN_BYTECODE_H_

#include <cstdint>

// qjsc 在构建期把 resources/*.js 编成字节码 C 数组（见 CMakeLists.txt 的
// MARKDOWN_JS_BYTECODE 段）。生成的 .c 是 C 源文件，符号是 C linkage，
// cpp 里要 extern "C"。
//
// 用法（见三个渲染器的 EnsureLoaded）：
//   JSValue obj = JS_ReadObject(ctx, md_hl, md_hl_size, JS_READ_OBJ_BYTECODE);
//   JSValue r = JS_EvalFunction(ctx, obj);  // 接管 obj 所有权，别再 Free obj
//
// 字节码与 QuickJS 版本、位数、编译开关绑定：只在同一构建树内由同一次
// 构建的 qjsc 生成，不跨构建目录复用、不入库
extern "C" {
extern const uint32_t md_hl_size;  // highlight.min.js
extern const uint8_t md_hl[];

extern const uint32_t md_mj_size;  // mathjax-svg.min.js
extern const uint8_t md_mj[];

extern const uint32_t md_mm_size;  // mermaid.js
extern const uint8_t md_mm[];

extern const uint32_t md_ds_size;  // dom_shim.js
extern const uint8_t md_ds[];

extern const uint32_t md_pb_size;  // path_bbox.js
extern const uint8_t md_pb[];
}

#endif  // FILE_MARKDOWN_MARKDOWN_BYTECODE_H_
