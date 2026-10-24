#ifndef FILE_MDHTML_MD_QUERIES_H_
#define FILE_MDHTML_MD_QUERIES_H_

#include <string_view>

// Highlights query text for a bundled tree-sitter language, generated at
// build time from 3rdparty/tree-sitter-lang. Empty when the language has no
// embedded query; the generated translation unit is the only provider.
std::string_view MdEmbeddedHighlightQuery(std::string_view name);

#endif  // FILE_MDHTML_MD_QUERIES_H_
