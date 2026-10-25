#ifndef FILE_MDHTML_MD_ASSEMBLER_H_
#define FILE_MDHTML_MD_ASSEMBLER_H_

#include <functional>
#include <string>
#include <string_view>

#include "md_types.h"

using MdFormulaSink =
    std::function<std::string(std::string_view tex, bool display)>;
using MdMermaidSink = std::function<std::string(std::string_view code)>;
using MdCodeBlockSink = std::function<void(
    std::string_view id, std::string_view language, std::string_view code)>;
using MdHeadingSink =
    std::function<void(std::string_view slug, std::string_view id)>;

// Assemble stage: replaces code blocks, math placeholders and headings.
// Resolver is checked by stable key; an empty string means render failure
// and the assembler falls back to escaped source / plain code block.
using MdRenderResolver =
    std::function<std::string(MdRenderKind kind, std::string_view payload,
                              std::string_view language, bool display)>;

struct MdAssembleOptions {
  bool code_line_numbers = false;
  std::function<std::string(std::string_view code, std::string_view language)>
      highlight;
  std::function<void(const MdHeading&)> outline;
};

std::string AssembleMarkdown(const MdParseResult& parsed,
                             const MdRenderResolver& resolver,
                             const MdCodeBlockSink& code_blocks,
                             const MdHeadingSink& heading_ids,
                             const MdAssembleOptions& options);

#endif  // FILE_MDHTML_MD_ASSEMBLER_H_
