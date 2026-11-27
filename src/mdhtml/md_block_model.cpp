#include "md_block_model.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

extern "C" {
#include "md4c.h"
}

namespace {

constexpr std::string_view kMathPrefix = "@@mj-";

bool IsAsciiSpace(char c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' ||
         c == '\v';
}

std::string_view TrimAscii(std::string_view text) {
  std::size_t begin = 0;
  while (begin < text.size() && IsAsciiSpace(text[begin])) {
    ++begin;
  }
  std::size_t end = text.size();
  while (end > begin && IsAsciiSpace(text[end - 1])) {
    --end;
  }
  return text.substr(begin, end - begin);
}

bool EqualsAsciiInsensitive(std::string_view text, std::string_view lower) {
  if (text.size() != lower.size()) {
    return false;
  }
  for (std::size_t i = 0; i < text.size(); ++i) {
    char c = text[i];
    if (c >= 'A' && c <= 'Z') {
      c = static_cast<char>(c - 'A' + 'a');
    }
    if (c != lower[i]) {
      return false;
    }
  }
  return true;
}

bool AttrEquals(const MD_ATTRIBUTE& attr, std::string_view text) {
  return attr.text != nullptr && attr.size == text.size() &&
         std::string_view(attr.text, attr.size) == text;
}

MdAdmonitionKind AdmonitionFromName(std::string_view name) {
  if (EqualsAsciiInsensitive(name, "note"))
    return MdAdmonitionKind::kNote;
  if (EqualsAsciiInsensitive(name, "info"))
    return MdAdmonitionKind::kInfo;
  if (EqualsAsciiInsensitive(name, "tip"))
    return MdAdmonitionKind::kTip;
  if (EqualsAsciiInsensitive(name, "important"))
    return MdAdmonitionKind::kImportant;
  if (EqualsAsciiInsensitive(name, "warning"))
    return MdAdmonitionKind::kWarning;
  if (EqualsAsciiInsensitive(name, "caution"))
    return MdAdmonitionKind::kCaution;
  if (EqualsAsciiInsensitive(name, "danger"))
    return MdAdmonitionKind::kDanger;
  if (EqualsAsciiInsensitive(name, "success"))
    return MdAdmonitionKind::kSuccess;
  if (EqualsAsciiInsensitive(name, "question"))
    return MdAdmonitionKind::kQuestion;
  return MdAdmonitionKind::kNone;
}

bool HasPrefix(std::string_view text, std::string_view prefix) {
  return text.size() >= prefix.size() &&
         text.substr(0, prefix.size()) == prefix;
}

MdMermaidDiagram MermaidFromFirstLine(std::string_view line) {
  line = TrimAscii(line);
  if (HasPrefix(line, "graph") || HasPrefix(line, "flowchart")) {
    return MdMermaidDiagram::kFlowchart;
  }
  if (HasPrefix(line, "sequenceDiagram"))
    return MdMermaidDiagram::kSequence;
  if (HasPrefix(line, "classDiagram"))
    return MdMermaidDiagram::kClass;
  if (HasPrefix(line, "stateDiagram"))
    return MdMermaidDiagram::kState;
  if (HasPrefix(line, "erDiagram"))
    return MdMermaidDiagram::kEr;
  if (HasPrefix(line, "gantt"))
    return MdMermaidDiagram::kGantt;
  if (HasPrefix(line, "pie"))
    return MdMermaidDiagram::kPie;
  if (HasPrefix(line, "journey"))
    return MdMermaidDiagram::kJourney;
  if (HasPrefix(line, "gitGraph"))
    return MdMermaidDiagram::kGitGraph;
  if (HasPrefix(line, "mindmap"))
    return MdMermaidDiagram::kMindmap;
  if (HasPrefix(line, "timeline"))
    return MdMermaidDiagram::kTimeline;
  return MdMermaidDiagram::kUnknown;
}

bool IsSkippedBlock(MD_BLOCKTYPE type) {
  switch (type) {
    case MD_BLOCK_DOC:
    case MD_BLOCK_THEAD:
    case MD_BLOCK_TBODY:
    case MD_BLOCK_TR:
    case MD_BLOCK_TH:
    case MD_BLOCK_TD:
    case MD_BLOCK_FOOTNOTE_DEF_SECTION:
    case MD_BLOCK_BLANK:
      return true;
    default:
      return false;
  }
}

bool ParseIndex(std::string_view text, int* value) {
  if (text.empty() || text.size() > 9) {
    return false;
  }
  int parsed = 0;
  for (char c : text) {
    if (c < '0' || c > '9') {
      return false;
    }
    parsed = parsed * 10 + (c - '0');
  }
  *value = parsed;
  return true;
}

struct PlaceholderState {
  std::string tail;
  bool seen = false;
  int count = 0;
  bool outside_text = false;
  bool last_display = false;
};

struct ParagraphState {
  int index = -1;
  bool has_image = false;
  bool mpe_capture = false;
  int mpe_quote = -1;
  std::string mpe_text;
  PlaceholderState placeholder;
};

struct QuoteState {
  int ordinal = -1;
  bool checked = false;
};

struct CodeState {
  int index = -1;
  std::string first_line;
  bool first_line_done = false;
};

struct Collector {
  const MdBlockCollectOptions& options;
  std::vector<MdBlock> blocks;
  std::vector<int> frames;
  ParagraphState paragraph;
  std::vector<QuoteState> quotes;
  CodeState code;
  int section = -1;
  int image_depth = 0;

  explicit Collector(const MdBlockCollectOptions& opts) : options(opts) {}

  int TopParent() const {
    for (auto it = frames.rbegin(); it != frames.rend(); ++it) {
      if (*it >= 0) {
        return *it;
      }
    }
    return -1;
  }

  int NearestAncestor(MdBlockKind kind) const {
    for (auto it = frames.rbegin(); it != frames.rend(); ++it) {
      if (*it >= 0 && blocks[static_cast<std::size_t>(*it)].kind == kind) {
        return *it;
      }
    }
    return -1;
  }

  bool IsAncestor(int ordinal) const {
    for (int frame : frames) {
      if (frame == ordinal) {
        return true;
      }
    }
    return false;
  }

  void FeedPlaceholder(std::string_view chunk) {
    PlaceholderState& state = paragraph.placeholder;
    std::string text = state.tail;
    text.append(chunk);
    state.tail.clear();

    std::size_t i = 0;
    while (i < text.size()) {
      if (HasPrefix(std::string_view(text).substr(i), kMathPrefix)) {
        const std::size_t close = text.find("@@", i + kMathPrefix.size());
        if (close == std::string::npos) {
          if (text.size() - i <= 64) {
            state.tail = text.substr(i);
          } else {
            state.outside_text = true;
          }
          return;
        }
        const std::string_view inner(text.data() + i + kMathPrefix.size(),
                                     close - (i + kMathPrefix.size()));
        const std::size_t dash = inner.rfind('-');
        int index = -1;
        if (dash != std::string_view::npos &&
            ParseIndex(inner.substr(dash + 1), &index) &&
            options.math_segments != nullptr && index >= 0 &&
            index < static_cast<int>(options.math_segments->size())) {
          state.seen = true;
          ++state.count;
          state.last_display = (*options.math_segments)[index].display;
        } else {
          state.outside_text = true;
        }
        i = close + 2;
        continue;
      }

      const std::string_view rest = std::string_view(text).substr(i);
      if (rest[0] == '@' && rest.size() < kMathPrefix.size() &&
          kMathPrefix.substr(0, rest.size()) == rest) {
        state.tail = std::string(rest);
        return;
      }

      if (!IsAsciiSpace(rest[0])) {
        state.outside_text = true;
      }
      ++i;
    }
  }

  void FinishParagraph() {
    if (paragraph.index < 0) {
      return;
    }
    MdBlock& block = blocks[static_cast<std::size_t>(paragraph.index)];
    const PlaceholderState& placeholder = paragraph.placeholder;
    if (placeholder.seen) {
      if (!placeholder.outside_text && placeholder.count == 1 &&
          placeholder.last_display) {
        block.kind = MdBlockKind::kMathBlock;
      } else {
        block.inline_flags |= kInlineMath;
      }
    } else if (!placeholder.outside_text && paragraph.has_image) {
      block.kind = MdBlockKind::kImage;
    }
    paragraph = ParagraphState();
  }

  void FinishCode() {
    if (code.index < 0) {
      return;
    }
    MdBlock& block = blocks[static_cast<std::size_t>(code.index)];
    if (block.kind == MdBlockKind::kMermaid) {
      block.mermaid_kind = MermaidFromFirstLine(code.first_line);
    }
    code = CodeState();
  }

  void FinishMpeCapture() {
    if (!paragraph.mpe_capture) {
      return;
    }
    MdAdmonitionKind kind = MdAdmonitionKind::kNone;
    std::string_view line = TrimAscii(paragraph.mpe_text);
    if (line.size() >= 4 && line.substr(0, 2) == "[!") {
      const std::size_t close = line.find(']');
      if (close != std::string_view::npos && close > 2) {
        const std::size_t after = close + 1;
        if (after == line.size() || IsAsciiSpace(line[after])) {
          kind = AdmonitionFromName(line.substr(2, close - 2));
        }
      }
    }
    for (auto it = quotes.rbegin(); it != quotes.rend(); ++it) {
      if (it->ordinal == paragraph.mpe_quote) {
        if (kind != MdAdmonitionKind::kNone) {
          MdBlock& quote = blocks[static_cast<std::size_t>(it->ordinal)];
          quote.kind = MdBlockKind::kAdmonition;
          quote.admonition = kind;
        }
        it->checked = true;
        break;
      }
    }
  }
};

MdBlockKind MapBlockKind(MD_BLOCKTYPE type, void* detail, MdBlock* block) {
  switch (type) {
    case MD_BLOCK_P:
      return MdBlockKind::kParagraph;
    case MD_BLOCK_H:
      block->level =
          static_cast<int>(static_cast<MD_BLOCK_H_DETAIL*>(detail)->level);
      return MdBlockKind::kHeading;
    case MD_BLOCK_QUOTE:
      return MdBlockKind::kBlockQuote;
    case MD_BLOCK_UL:
    case MD_BLOCK_OL:
      return MdBlockKind::kList;
    case MD_BLOCK_LI:
      return static_cast<MD_BLOCK_LI_DETAIL*>(detail)->is_task
                 ? MdBlockKind::kTaskItem
                 : MdBlockKind::kListItem;
    case MD_BLOCK_HR:
      return MdBlockKind::kThematicBreak;
    case MD_BLOCK_CODE: {
      const auto* code = static_cast<MD_BLOCK_CODE_DETAIL*>(detail);
      if (AttrEquals(code->lang, "mermaid")) {
        return MdBlockKind::kMermaid;
      }
      return MdBlockKind::kCodeBlock;
    }
    case MD_BLOCK_TABLE: {
      const auto* table = static_cast<MD_BLOCK_TABLE_DETAIL*>(detail);
      block->children =
          static_cast<int>(table->head_row_count + table->body_row_count);
      return MdBlockKind::kTable;
    }
    case MD_BLOCK_HTML:
      return MdBlockKind::kHtmlBlock;
    case MD_BLOCK_ADMONITION: {
      const auto* adm = static_cast<MD_BLOCK_ADMONITION_DETAIL*>(detail);
      const std::string_view name(
          adm->type.text != nullptr ? adm->type.text : "",
          adm->type.text != nullptr ? adm->type.size : 0);
      block->admonition = AdmonitionFromName(name);
      if (block->admonition == MdAdmonitionKind::kNone) {
        block->admonition = MdAdmonitionKind::kNote;
      }
      return MdBlockKind::kAdmonition;
    }
    case MD_BLOCK_FOOTNOTE_DEF:
      return MdBlockKind::kFootnoteDef;
    default:
      return MdBlockKind::kOther;
  }
}

int EnterBlock(MD_BLOCKTYPE type, void* detail, void* userdata) {
  auto* collector = static_cast<Collector*>(userdata);
  if (IsSkippedBlock(type)) {
    collector->frames.push_back(-1);
    return 0;
  }

  const int parent = collector->TopParent();
  if (type == MD_BLOCK_H && parent == -1) {
    ++collector->section;
  }

  MdBlock block;
  block.ordinal = static_cast<int>(collector->blocks.size());
  block.parent = parent;
  block.section = collector->section;
  block.kind = MapBlockKind(type, detail, &block);
  if (type == MD_BLOCK_CODE) {
    const auto* code = static_cast<MD_BLOCK_CODE_DETAIL*>(detail);
    if (code->lang.text != nullptr && code->lang.size > 0) {
      block.language.assign(code->lang.text, code->lang.size);
    }
  }

  if (type == MD_BLOCK_LI) {
    const int list = collector->NearestAncestor(MdBlockKind::kList);
    if (list >= 0) {
      ++collector->blocks[static_cast<std::size_t>(list)].children;
    }
  }

  const MdBlockKind kind = block.kind;
  collector->blocks.push_back(std::move(block));
  const int ordinal = collector->blocks.back().ordinal;
  collector->frames.push_back(ordinal);

  if (type == MD_BLOCK_P) {
    collector->paragraph = ParagraphState();
    collector->paragraph.index = ordinal;
    if (!collector->quotes.empty() && !collector->quotes.back().checked &&
        collector->IsAncestor(collector->quotes.back().ordinal)) {
      collector->paragraph.mpe_capture = true;
      collector->paragraph.mpe_quote = collector->quotes.back().ordinal;
    }
  } else if (type == MD_BLOCK_QUOTE) {
    collector->quotes.push_back({ordinal, false});
  } else if (type == MD_BLOCK_CODE && kind == MdBlockKind::kMermaid) {
    collector->code = CodeState();
    collector->code.index = ordinal;
  }
  return 0;
}

int LeaveBlock(MD_BLOCKTYPE type, void* detail, void* userdata) {
  (void)detail;
  auto* collector = static_cast<Collector*>(userdata);
  if (type == MD_BLOCK_P) {
    collector->FinishMpeCapture();
    collector->FinishParagraph();
  } else if (type == MD_BLOCK_CODE) {
    collector->FinishCode();
  } else if (type == MD_BLOCK_QUOTE) {
    if (!collector->quotes.empty()) {
      collector->quotes.pop_back();
    }
  }
  if (!collector->frames.empty()) {
    collector->frames.pop_back();
  }
  return 0;
}

int EnterSpan(MD_SPANTYPE type, void* detail, void* userdata) {
  (void)detail;
  auto* collector = static_cast<Collector*>(userdata);
  if (type == MD_SPAN_IMG) {
    ++collector->image_depth;
    if (collector->paragraph.index >= 0) {
      collector->paragraph.has_image = true;
    }
  }
  if (collector->frames.empty()) {
    return 0;
  }
  const int current = collector->frames.back();
  if (current < 0) {
    return 0;
  }
  std::uint32_t flags = 0;
  switch (type) {
    case MD_SPAN_STRONG:
      flags = kInlineBold;
      break;
    case MD_SPAN_EM:
      flags = kInlineItalic;
      break;
    case MD_SPAN_DEL:
      flags = kInlineStrike;
      break;
    case MD_SPAN_CODE:
      flags = kInlineCode;
      break;
    case MD_SPAN_A:
      flags = kInlineLink;
      break;
    case MD_SPAN_IMG:
      flags = kInlineImage;
      break;
    case MD_SPAN_MARK:
      flags = kInlineMark;
      break;
    case MD_SPAN_SUPERSCRIPT:
      flags = kInlineSup;
      break;
    case MD_SPAN_SUBSCRIPT:
      flags = kInlineSub;
      break;
    case MD_SPAN_INS:
      flags = kInlineInsert;
      break;
    case MD_SPAN_FOOTNOTE_REF:
      flags = kInlineFootnote;
      break;
    default:
      break;
  }
  collector->blocks[static_cast<std::size_t>(current)].inline_flags |= flags;
  return 0;
}

int LeaveSpan(MD_SPANTYPE type, void* detail, void* userdata) {
  (void)detail;
  auto* collector = static_cast<Collector*>(userdata);
  if (type == MD_SPAN_IMG && collector->image_depth > 0) {
    --collector->image_depth;
  }
  return 0;
}

int OnText(MD_TEXTTYPE type, const MD_CHAR* text, MD_SIZE size,
           void* userdata) {
  auto* collector = static_cast<Collector*>(userdata);
  if (collector->frames.empty()) {
    return 0;
  }
  const int current = collector->frames.back();
  if (current < 0) {
    return 0;
  }
  if (type == MD_TEXT_BR || type == MD_TEXT_SOFTBR) {
    return 0;
  }

  if (collector->paragraph.index == current) {
    if (collector->image_depth == 0) {
      collector->FeedPlaceholder(std::string_view(text, size));
    }
    if (collector->paragraph.mpe_capture && type == MD_TEXT_NORMAL &&
        collector->paragraph.mpe_text.size() < 160) {
      const std::size_t room = 160 - collector->paragraph.mpe_text.size();
      collector->paragraph.mpe_text.append(text,
                                           std::min<std::size_t>(room, size));
    }
    return 0;
  }

  if (collector->code.index == current && type == MD_TEXT_CODE &&
      !collector->code.first_line_done) {
    for (MD_SIZE i = 0; i < size; ++i) {
      const char c = text[i];
      if (c == '\n') {
        collector->code.first_line_done = true;
        break;
      }
      if (collector->code.first_line.size() < 64) {
        collector->code.first_line.push_back(c);
      }
    }
  }
  return 0;
}

}  // namespace

std::vector<MdBlock> CollectDocumentBlocks(
    const MdBlockCollectOptions& options) {
  Collector collector(options);
  MD_PARSER parser = {};
  parser.flags = options.flags;
  parser.enter_block = EnterBlock;
  parser.leave_block = LeaveBlock;
  parser.enter_span = EnterSpan;
  parser.leave_span = LeaveSpan;
  parser.text = OnText;
  md_parse(options.source.data(), static_cast<MD_SIZE>(options.source.size()),
           &parser, &collector);
  return std::move(collector.blocks);
}
