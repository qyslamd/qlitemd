#ifndef FILE_MDHTML_MD_TYPES_H_
#define FILE_MDHTML_MD_TYPES_H_

#include <cstdint>
#include <string>
#include <vector>

enum class MdTheme { Light, Dark };

struct MdColor {
  uint8_t r = 0;
  uint8_t g = 0;
  uint8_t b = 0;
  uint8_t a = 255;
};

struct MdHeading {
  int level = 0;
  std::string text;
  std::string anchor;
};

struct MdCodeBlock {
  int index = 0;
  std::string language;
  std::string code;
};

struct MdMathSegment {
  std::string tex;
  bool display = false;
};

struct MdParseResult {
  std::string html;
  std::string math_salt;
  std::vector<MdMathSegment> segments;
  std::vector<MdCodeBlock> blocks;
  bool parse_error = false;
};

enum class MdRenderKind { Highlight, Formula, Mermaid };

#endif  // FILE_MDHTML_MD_TYPES_H_
