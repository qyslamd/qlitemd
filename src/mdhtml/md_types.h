#ifndef FILE_MDHTML_MD_TYPES_H_
#define FILE_MDHTML_MD_TYPES_H_

#include <cstdint>
#include <string>
#include <vector>

enum class MdTheme { Light, Dark };

enum class MdAdmonitionMode {
  kGithub = 0,  // md4c's GitHub admonitions
  kMpe = 1,     // MPE callouts (inline text is the title)
};

// Which Mermaid backend renders a diagram. Availability depends on the
// build: a backend not compiled in degrades to the other one.
enum class MdMermaidBackend {
  kAuto = 0,     // native (merman) first, per-diagram quickjs fallback
  kNative = 1,   // merman only
  kQuickjs = 2,  // mermaid.js in QuickJS only
};

// Which TeX backend renders a formula. RaTeX is the native FFI backend;
// QuickJS runs MathJax. Availability follows the same degradation rule as
// Mermaid.
enum class MdFormulaBackend {
  kAuto = 0,     // native (RaTeX) first, per-formula quickjs fallback
  kNative = 1,   // RaTeX only
  kQuickjs = 2,  // MathJax in QuickJS only
};

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
  // ASCII element id ("h-0"), stable for scroll/anchor lookup.
  std::string id;
};

struct MdCodeBlock {
  int index = 0;
  // Assembler block id; matches the copy:<id> link in the HTML.
  std::string id;
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

enum class MdAssetKind { Formula, Mermaid };

// kPortable keeps the SVG as produced (root extraction, formula colour);
// kQtSvg is reserved for hosts that still rasterize with QSvgRenderer and
// need the QtSvg compatibility pass applied in their adapter layer.
enum class MdSvgProfile { kPortable, kQtSvg };

struct MdSvg {
  std::string svg;
  int width = 0;
  int height = 0;
};

struct MdAsset {
  std::string id;
  MdAssetKind kind = MdAssetKind::Formula;
  std::string source;
  MdSvg image;
  bool display = false;
};

#endif  // FILE_MDHTML_MD_TYPES_H_
