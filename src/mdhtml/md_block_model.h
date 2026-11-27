#ifndef FILE_MDHTML_MD_BLOCK_MODEL_H_
#define FILE_MDHTML_MD_BLOCK_MODEL_H_

#include <string_view>
#include <vector>

#include "md_types.h"

// Structural block model for hosts that need more than HTML: instruments,
// section segmentation and the C ABI. The collector runs a second md_parse
// pass over the same source md_html sees, so both views agree.
struct MdBlockCollectOptions {
  // Post-math-expansion source (formulas already replaced by placeholders).
  std::string_view source;
  unsigned flags = 0;
  MdAdmonitionMode admonition_mode = MdAdmonitionMode::kMpe;
  // Needed to tell display math from inline math placeholders.
  const std::vector<MdMathSegment>* math_segments = nullptr;
  std::string_view math_salt;
};

std::vector<MdBlock> CollectDocumentBlocks(
    const MdBlockCollectOptions& options);

#endif  // FILE_MDHTML_MD_BLOCK_MODEL_H_
