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

// Stable block vocabulary shared by the parser, the Qt view and the C ABI.
// Values are part of the ABI: append only, never renumber.
enum class MdBlockKind : std::uint8_t {
  kParagraph = 0,
  kHeading = 1,
  kBlockQuote = 2,
  kList = 3,
  kListItem = 4,
  kTaskItem = 5,
  kCodeBlock = 6,
  kMathBlock = 7,
  kMermaid = 8,
  kTable = 9,
  kImage = 10,
  kHtmlBlock = 11,
  kAdmonition = 12,
  kFootnoteDef = 13,
  kThematicBreak = 14,
  kOther = 15,
};

enum class MdMermaidDiagram : std::uint8_t {
  kUnknown = 0,
  kFlowchart = 1,
  kSequence = 2,
  kClass = 3,
  kState = 4,
  kEr = 5,
  kGantt = 6,
  kPie = 7,
  kJourney = 8,
  kGitGraph = 9,
  kMindmap = 10,
  kTimeline = 11,
};

enum class MdAdmonitionKind : std::uint8_t {
  kNone = 0,
  kNote = 1,
  kInfo = 2,
  kTip = 3,
  kImportant = 4,
  kWarning = 5,
  kCaution = 6,
  kDanger = 7,
  kSuccess = 8,
  kQuestion = 9,
};

enum MdInlineFlag : std::uint32_t {
  kInlineBold = 1u << 0,
  kInlineItalic = 1u << 1,
  kInlineStrike = 1u << 2,
  kInlineCode = 1u << 3,
  kInlineMath = 1u << 4,
  kInlineLink = 1u << 5,
  kInlineImage = 1u << 6,
  kInlineMark = 1u << 7,
  kInlineSup = 1u << 8,
  kInlineSub = 1u << 9,
  kInlineInsert = 1u << 10,
  kInlineFootnote = 1u << 11,
};

struct MdBlock {
  MdBlockKind kind = MdBlockKind::kOther;
  // Document order among emitted blocks. Table internals are skipped, so
  // ordinals are contiguous.
  int ordinal = 0;
  // Ordinal of the nearest emitted ancestor, -1 at the top level.
  int parent = -1;
  // Index of the section opened by the nearest preceding top-level heading;
  // -1 before the first heading.
  int section = -1;
  int level = 0;
  MdAdmonitionKind admonition = MdAdmonitionKind::kNone;
  MdMermaidDiagram mermaid_kind = MdMermaidDiagram::kUnknown;
  std::uint32_t inline_flags = 0;
  // List items, table rows, etc.
  int children = 0;
  std::string language;
};

struct MdFrontMatterEntry {
  std::string key;
  std::string value;
};

struct MdParseResult {
  std::string html;
  std::string math_salt;
  std::vector<MdMathSegment> segments;
  std::vector<MdCodeBlock> blocks;
  std::vector<MdBlock> document_blocks;
  std::vector<MdFrontMatterEntry> front_matter;
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
