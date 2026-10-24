#ifndef FILE_MDHTML_MD_BYTECODE_H_
#define FILE_MDHTML_MD_BYTECODE_H_

#include <cstdint>

// qjsc bytecode arrays generated at build time from resources/*.js (see
// src/mdhtml/CMakeLists.txt). Only referenced by the QuickJS fallback.
extern "C" {
extern const uint32_t md_mj_size;
extern const uint8_t md_mj[];
extern const uint32_t md_mm_size;
extern const uint8_t md_mm[];
extern const uint32_t md_ds_size;
extern const uint8_t md_ds[];
extern const uint32_t md_pb_size;
extern const uint8_t md_pb[];
}

#endif  // FILE_MDHTML_MD_BYTECODE_H_
