#ifndef FILE_MDHTML_MD_SVG_H_
#define FILE_MDHTML_MD_SVG_H_

#include <cstddef>
#include <string>
#include <string_view>

#include "md_types.h"

// Per-asset SVG cap: an accidental huge payload must fail the asset instead
// of exhausting memory in every worker.
constexpr std::size_t kMdMaxSvgBytes = 8u * 1024u * 1024u;

// Returns the substring from the first "<svg" through the last "</svg>"
// (inclusive), dropping XML declarations and wrapper markup. Empty when no
// SVG root is present or the input exceeds kMdMaxSvgBytes.
std::string MdExtractSvgRoot(std::string_view svg);

// Intrinsic CSS-pixel size: a usable viewBox wins, otherwise numeric
// width/height attributes (unitless or px). Percent and font-relative sizes
// are not intrinsic and return false when no viewBox exists.
bool MdSvgIntrinsicSize(std::string_view svg, int* width, int* height);

// Lowercase "#rrggbb"; alpha is not part of SVG paint strings.
std::string MdColorHex(MdColor color);

// Portable-profile colour handling: replaces every currentColor with the
// opaque #rrggbb value, or injects fill on the first group when the payload
// carries no currentColor and no explicit fill. Alpha is ignored.
std::string MdApplySvgColor(std::string_view svg, MdColor color);

#endif  // FILE_MDHTML_MD_SVG_H_
