#ifndef FILE_MDHTML_MD_HASH_H_
#define FILE_MDHTML_MD_HASH_H_

#include <string>
#include <string_view>

#include "md_types.h"

// Lowercase hex SHA-256. Kept in the core so asset ids and future persistent
// cache keys are deterministic across processes and platforms.
std::string MdSha256Hex(std::string_view data);

// Stable asset id: the first 128 bits of SHA-256 over the kind tag and key.
// Kind separation keeps identical TeX/Mermaid sources from colliding.
std::string MdAssetId(MdAssetKind kind, std::string_view key);

#endif  // FILE_MDHTML_MD_HASH_H_
