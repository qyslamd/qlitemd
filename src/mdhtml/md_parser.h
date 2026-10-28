#ifndef FILE_MDHTML_MD_PARSER_H_
#define FILE_MDHTML_MD_PARSER_H_

#include <string>
#include <string_view>

#include "md_types.h"

// Parses GitHub-flavoured Markdown with md4c. When extract_math is true,
// $...$ / $$...$$ are replaced by salted placeholders and collected in
// segments; the placeholders are restored by the assembler.
// kGithub uses md4c's admonitions; kMpe rewrites blockquotes into callouts.
MdParseResult ParseMarkdown(
    std::string_view markdown, bool extract_math,
    MdAdmonitionMode admonition_mode = MdAdmonitionMode::kMpe);

#endif  // FILE_MDHTML_MD_PARSER_H_
