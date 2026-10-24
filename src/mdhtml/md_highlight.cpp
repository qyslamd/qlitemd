#include "md_highlight.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <regex>
#include <string>
#include <tree_sitter/api.h>
#include <unordered_map>
#include <utility>
#include <vector>

#include "md_queries.h"

extern "C" {
const TSLanguage* tree_sitter_bash(void);
const TSLanguage* tree_sitter_batch(void);
const TSLanguage* tree_sitter_c(void);
const TSLanguage* tree_sitter_cpp(void);
const TSLanguage* tree_sitter_css(void);
const TSLanguage* tree_sitter_go(void);
const TSLanguage* tree_sitter_html(void);
const TSLanguage* tree_sitter_java(void);
const TSLanguage* tree_sitter_javascript(void);
const TSLanguage* tree_sitter_json(void);
const TSLanguage* tree_sitter_python(void);
const TSLanguage* tree_sitter_rust(void);
}

namespace {

// Capture name -> short CSS class. The set is the contract with
// resources/themes/*.css; unmapped captures stay uncoloured.
std::string MapCapture(std::string_view capture) {
  struct Entry {
    const char* name;
    const char* css;
  };
  static constexpr Entry kExact[] = {
      {"keyword", "kw"},
      {"keyword.operator", "op"},
      {"keyword.return", "kw"},
      {"string", "str"},
      {"string.escape", "str"},
      {"number", "num"},
      {"number.float", "num"},
      {"comment", "cmt"},
      {"comment.documentation", "cmt"},
      {"type", "ty"},
      {"type.builtin", "ty"},
      {"function", "fn"},
      {"function.call", "fn"},
      {"function.method", "fn"},
      {"variable.parameter", "var"},
      {"operator", "op"},
      {"punctuation", "op"},
      {"constant", "num"},
      {"property", "attr"},
  };
  for (const Entry& entry : kExact) {
    if (capture == entry.name) {
      return entry.css;
    }
  }
  // Query capture names often carry a hierarchy suffix (keyword.modifier,
  // string.special, ...). Prefer the longest matching prefix so the result
  // does not depend on table order.
  std::string_view best;
  const char* best_css = nullptr;
  for (const Entry& entry : kExact) {
    const std::string_view name = entry.name;
    if (capture.size() > name.size() &&
        capture.substr(0, name.size()) == name && name.size() > best.size()) {
      best = name;
      best_css = entry.css;
    }
  }
  return best_css == nullptr ? std::string() : std::string(best_css);
}

// The highlighter has always emitted &#x27; for apostrophes (the themes and
// the markdown assembly tests pin that form); md_text's EscapeHtml leaves
// apostrophes alone, so keep a local variant.
std::string EscapeHighlighted(std::string_view text) {
  std::string out;
  out.reserve(text.size());
  for (char c : text) {
    switch (c) {
      case '&':
        out += "&amp;";
        break;
      case '<':
        out += "&lt;";
        break;
      case '>':
        out += "&gt;";
        break;
      case '"':
        out += "&quot;";
        break;
      case '\'':
        out += "&#x27;";
        break;
      default:
        out.push_back(c);
        break;
    }
  }
  return out;
}

struct PredicateRule {
  enum Type { kMatch, kEq, kNeq, kAnyEq, kAnyNeq };
  Type type = kEq;
  uint32_t lhs = 0;
  bool rhs_is_capture = false;
  uint32_t rhs_capture = 0;
  bool regex_valid = false;
  std::regex regex;
  std::string text;
};

// std::regex has no inline-flag syntax; the bundled queries only use a
// leading (?i), which maps to icase.
std::regex CompileQueryRegex(const std::string& pattern, bool* valid) {
  std::string body = pattern;
  auto flags = std::regex::ECMAScript;
  if (body.rfind("(?i)", 0) == 0) {
    body.erase(0, 4);
    flags |= std::regex::icase;
  }
  try {
    std::regex compiled(body, flags);
    *valid = true;
    return compiled;
  } catch (const std::regex_error&) {
    *valid = false;
    return std::regex();
  }
}

// Predicate subset used by the bundled queries: #match? @cap "re",
// #eq? @a (@b|"s"), #ne?, #any-eq?, #any-not-eq?. Rules AND together.
std::vector<PredicateRule> ParsePredicates(const TSQuery* query,
                                           uint32_t pattern_index) {
  uint32_t length = 0;
  const TSQueryPredicateStep* steps =
      ts_query_predicates_for_pattern(query, pattern_index, &length);
  std::vector<PredicateRule> rules;
  PredicateRule current;
  bool has_type = false;
  bool has_lhs = false;
  bool has_rhs = false;
  for (uint32_t i = 0; i < length; ++i) {
    const TSQueryPredicateStep& step = steps[i];
    switch (step.type) {
      case TSQueryPredicateStepTypeDone:
        if (has_type && has_lhs) {
          rules.push_back(std::move(current));
        }
        current = PredicateRule();
        has_type = has_lhs = has_rhs = false;
        break;
      case TSQueryPredicateStepTypeCapture:
        if (!has_lhs) {
          current.lhs = step.value_id;
          has_lhs = true;
        } else if (!has_rhs) {
          current.rhs_is_capture = true;
          current.rhs_capture = step.value_id;
          has_rhs = true;
        }
        break;
      case TSQueryPredicateStepTypeString: {
        uint32_t string_length = 0;
        const char* value =
            ts_query_string_value_for_id(query, step.value_id, &string_length);
        const std::string text(value, string_length);
        // The first string step is the predicate name; later ones operands.
        if (!has_type) {
          if (text == "match?") {
            current.type = PredicateRule::kMatch;
          } else if (text == "eq?") {
            current.type = PredicateRule::kEq;
          } else if (text == "ne?") {
            current.type = PredicateRule::kNeq;
          } else if (text == "any-eq?") {
            current.type = PredicateRule::kAnyEq;
          } else if (text == "any-not-eq?") {
            current.type = PredicateRule::kAnyNeq;
          }
          // Unknown predicates keep the kEq default. The bundled JavaScript
          // query uses #is-not? (scope property lookup, which this engine
          // does not track); the previous Qt module evaluated it the same
          // way, and rendering parity depends on keeping that behaviour.
          has_type = true;
        } else if (!has_rhs) {
          if (current.type == PredicateRule::kMatch) {
            current.regex = CompileQueryRegex(text, &current.regex_valid);
          } else {
            current.text = text;
          }
          has_rhs = true;
        }
        break;
      }
      default:
        break;
    }
  }
  return rules;
}

struct LanguageInfo {
  const TSLanguage* language = nullptr;
  TSQuery* query = nullptr;
  std::vector<std::string> cap_classes;
  std::unordered_map<uint32_t, std::vector<PredicateRule>> predicates;
};

// Process-wide read-only registry. TSQuery is immutable after creation and
// std::regex matching on a const object is thread-safe, so workers only
// need their own parsers.
const std::unordered_map<std::string, LanguageInfo>& Registry() {
  static const std::unordered_map<std::string, LanguageInfo> registry = [] {
    struct Entry {
      const char* name;
      const TSLanguage* (*language)(void);
      // cpp's highlights extend c's captures (grammar inheritance).
      const char* inherit;
    };
    const Entry entries[] = {
        {"bash", tree_sitter_bash, nullptr},
        {"batch", tree_sitter_batch, nullptr},
        {"c", tree_sitter_c, nullptr},
        {"cpp", tree_sitter_cpp, "c"},
        {"css", tree_sitter_css, nullptr},
        {"go", tree_sitter_go, nullptr},
        {"html", tree_sitter_html, nullptr},
        {"java", tree_sitter_java, nullptr},
        {"javascript", tree_sitter_javascript, nullptr},
        {"json", tree_sitter_json, nullptr},
        {"python", tree_sitter_python, nullptr},
        {"rust", tree_sitter_rust, nullptr},
    };

    std::unordered_map<std::string, LanguageInfo> map;
    for (const Entry& entry : entries) {
      std::string scm;
      if (entry.inherit != nullptr) {
        const std::string_view parent = MdEmbeddedHighlightQuery(entry.inherit);
        if (!parent.empty()) {
          scm.assign(parent);
          scm.push_back('\n');
        }
      }
      const std::string_view own = MdEmbeddedHighlightQuery(entry.name);
      if (own.empty()) {
        // A missing query degrades this language to plain escaping; it must
        // not take the other languages down with it.
        continue;
      }
      scm.append(own);

      uint32_t error_offset = 0;
      TSQueryError error = TSQueryErrorNone;
      TSQuery* query = ts_query_new(entry.language(), scm.data(),
                                    static_cast<uint32_t>(scm.size()),
                                    &error_offset, &error);
      if (query == nullptr) {
        continue;
      }
      LanguageInfo info;
      info.language = entry.language();
      info.query = query;
      info.cap_classes.resize(ts_query_capture_count(query));
      for (uint32_t i = 0; i < info.cap_classes.size(); ++i) {
        uint32_t name_length = 0;
        const char* capture =
            ts_query_capture_name_for_id(query, i, &name_length);
        info.cap_classes[i] = MapCapture(
            std::string_view(capture, static_cast<std::size_t>(name_length)));
      }
      const uint32_t pattern_count = ts_query_pattern_count(query);
      for (uint32_t pattern = 0; pattern < pattern_count; ++pattern) {
        std::vector<PredicateRule> rules = ParsePredicates(query, pattern);
        if (!rules.empty()) {
          info.predicates.emplace(pattern, std::move(rules));
        }
      }
      map.emplace(entry.name, std::move(info));
    }
    return map;
  }();
  return registry;
}

// Fence tag -> registry name. TypeScript falls back to JavaScript, which is
// acceptable under error-tolerant parsing.
std::string ResolveAlias(std::string_view language) {
  struct Alias {
    const char* from;
    const char* to;
  };
  static constexpr Alias kAliases[] = {
      {"js", "javascript"}, {"ts", "javascript"}, {"py", "python"},
      {"sh", "bash"},       {"shell", "bash"},    {"c++", "cpp"},
      {"bat", "batch"},     {"cmd", "batch"},
  };
  for (const Alias& alias : kAliases) {
    if (language == alias.from) {
      return alias.to;
    }
  }
  return std::string(language);
}

// Predicate semantics match the previous module: #match? requires every
// occurrence to match; #eq?/#ne? follow the "all/none" reading; a rule whose
// capture is absent from the match is skipped rather than failing.
bool PredicatesSatisfied(const LanguageInfo& info, const TSQueryMatch& match,
                         std::string_view source) {
  const auto rules_it = info.predicates.find(match.pattern_index);
  if (rules_it == info.predicates.end()) {
    return true;
  }
  auto text_of = [&](uint32_t capture_id, std::vector<std::string_view>* out) {
    for (uint16_t i = 0; i < match.capture_count; ++i) {
      if (match.captures[i].index != capture_id) {
        continue;
      }
      const TSNode& node = match.captures[i].node;
      const uint32_t start = ts_node_start_byte(node);
      const uint32_t end = ts_node_end_byte(node);
      out->push_back(source.substr(start, end - start));
    }
  };

  for (const PredicateRule& rule : rules_it->second) {
    std::vector<std::string_view> lhs;
    text_of(rule.lhs, &lhs);
    if (lhs.empty()) {
      continue;
    }
    std::vector<std::string_view> rhs;
    if (rule.rhs_is_capture) {
      text_of(rule.rhs_capture, &rhs);
    }
    const std::string_view rhs_text =
        rule.rhs_is_capture ? (rhs.empty() ? std::string_view() : rhs.front())
                            : std::string_view(rule.text);

    bool satisfied = false;
    switch (rule.type) {
      case PredicateRule::kMatch:
        satisfied =
            std::all_of(lhs.begin(), lhs.end(), [&](std::string_view t) {
              if (!rule.regex_valid) {
                return false;
              }
              return std::regex_search(t.begin(), t.end(), rule.regex);
            });
        break;
      case PredicateRule::kEq:
        satisfied =
            std::all_of(lhs.begin(), lhs.end(),
                        [&](std::string_view t) { return t == rhs_text; });
        break;
      case PredicateRule::kNeq:
        satisfied =
            std::none_of(lhs.begin(), lhs.end(),
                         [&](std::string_view t) { return t == rhs_text; });
        break;
      case PredicateRule::kAnyEq:
        satisfied =
            std::any_of(lhs.begin(), lhs.end(),
                        [&](std::string_view t) { return t == rhs_text; });
        break;
      case PredicateRule::kAnyNeq:
        satisfied =
            std::any_of(lhs.begin(), lhs.end(),
                        [&](std::string_view t) { return t != rhs_text; });
        break;
    }
    if (!satisfied) {
      return false;
    }
  }
  return true;
}

}  // namespace

struct MdTreeSitterParserCache::Impl {
  // TSParser is not thread-safe: this cache belongs to one worker thread.
  std::unordered_map<std::string, TSParser*> parsers;
};

MdTreeSitterParserCache::MdTreeSitterParserCache() : impl_(new Impl) {}

MdTreeSitterParserCache::~MdTreeSitterParserCache() {
  for (auto& entry : impl_->parsers) {
    ts_parser_delete(entry.second);
  }
}

std::string MdHighlightCode(std::string_view code, std::string_view language,
                            MdTreeSitterParserCache* cache) {
  if (code.empty()) {
    return {};
  }

  const std::string registered = ResolveAlias(language);
  const auto& registry = Registry();
  const auto info_it = registry.find(registered);
  if (info_it == registry.end()) {
    // Outside the whitelist: escape only, no language guessing (a wrong guess
    // looks worse than no highlighting).
    return EscapeHighlighted(code);
  }
  const LanguageInfo& info = info_it->second;

  TSParser* parser = nullptr;
  std::unique_ptr<TSParser, void (*)(TSParser*)> temp_parser(
      nullptr, [](TSParser* p) { ts_parser_delete(p); });
  if (cache != nullptr) {
    auto& parsers = cache->impl_->parsers;
    const auto parser_it = parsers.find(registered);
    if (parser_it != parsers.end()) {
      parser = parser_it->second;
    } else {
      parser = ts_parser_new();
      if (parser != nullptr) {
        parsers.emplace(registered, parser);
      }
    }
  } else {
    temp_parser.reset(ts_parser_new());
    parser = temp_parser.get();
  }
  if (parser == nullptr || !ts_parser_set_language(parser, info.language)) {
    return EscapeHighlighted(code);
  }

  TSTree* tree = ts_parser_parse_string(parser, nullptr, code.data(),
                                        static_cast<uint32_t>(code.size()));
  if (tree == nullptr) {
    return EscapeHighlighted(code);
  }

  TSQueryCursor* cursor = ts_query_cursor_new();
  ts_query_cursor_exec(cursor, info.query, ts_tree_root_node(tree));

  struct CaptureRange {
    uint32_t start = 0;
    uint32_t end = 0;
    const std::string* css = nullptr;
  };
  std::vector<CaptureRange> captures;
  TSQueryMatch match;
  while (ts_query_cursor_next_match(cursor, &match)) {
    if (!PredicatesSatisfied(info, match, code)) {
      continue;
    }
    for (uint16_t i = 0; i < match.capture_count; ++i) {
      const TSQueryCapture& capture = match.captures[i];
      const std::string& css = info.cap_classes[capture.index];
      if (css.empty()) {
        continue;
      }
      captures.push_back({ts_node_start_byte(capture.node),
                          ts_node_end_byte(capture.node), &css});
    }
  }
  ts_query_cursor_delete(cursor);
  ts_tree_delete(tree);

  // When nested captures cover the same byte, the innermost one wins.
  std::sort(captures.begin(), captures.end(),
            [](const CaptureRange& a, const CaptureRange& b) {
              if (a.start != b.start) {
                return a.start < b.start;
              }
              return a.end > b.end;
            });

  std::vector<int> class_map(code.size(), -1);
  for (std::size_t i = 0; i < captures.size(); ++i) {
    for (uint32_t j = captures[i].start; j < captures[i].end; ++j) {
      class_map[j] = static_cast<int>(i);
    }
  }

  // Contract: after splitting on \n every line balances its spans. Captures
  // that cross a newline (block comments, strings) are closed at the break
  // and reopened on the next line.
  std::string out;
  out.reserve(code.size() * 2);
  std::string current_class;
  for (std::size_t i = 0; i < code.size(); ++i) {
    const char c = code[i];
    if (c == '\n') {
      if (!current_class.empty()) {
        out += "</span>";
        current_class.clear();
      }
      out += '\n';
      continue;
    }
    const int index = class_map[i];
    const std::string& css =
        index != -1 ? *captures[static_cast<std::size_t>(index)].css
                    : std::string();
    if (css != current_class) {
      if (!current_class.empty()) {
        out += "</span>";
      }
      if (!css.empty()) {
        out += "<span class=\"";
        out += css;
        out += "\">";
      }
      current_class = css;
    }
    switch (c) {
      case '&':
        out += "&amp;";
        break;
      case '<':
        out += "&lt;";
        break;
      case '>':
        out += "&gt;";
        break;
      case '"':
        out += "&quot;";
        break;
      case '\'':
        out += "&#x27;";
        break;
      default:
        out += c;
    }
  }
  if (!current_class.empty()) {
    out += "</span>";
  }
  return out;
}

std::string MdHighlightRenderKey(std::string_view code,
                                 std::string_view language) {
  std::string key(language);
  key.push_back('\n');
  key.append(code);
  return key;
}
