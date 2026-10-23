#include "tree_sitter_highlighter.h"

#include <algorithm>
#include <memory>
#include <string>
#include <tree_sitter/api.h>
#include <vector>

#include <QFile>
#include <QHash>
#include <QRegularExpression>
#include <QString>
#include <QtCore/qglobal.h>  // Q_INIT_RESOURCE

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

// Q_INIT_RESOURCE 展开成块作用域的 extern 声明，必须留在全局命名空间：
// 放进匿名命名空间会得到内部链接，GCC 链接期报 undefined reference
// （MSVC 宽容，Windows 构建不暴露）
static void EnsureHighlightResources() { Q_INIT_RESOURCE(markdown_res); }

namespace {

// 捕获名 → 主题 CSS 短类名。集合是主题样式表（resources/themes/*.css）
// 的契约，与 hljs 时代的 CLASS_MAP 输出对齐；未列出的捕获不着色
std::string MapCapture(const std::string& cap) {
  static const QHash<QString, const char*> kExact = {
      {QStringLiteral("keyword"), "kw"},
      {QStringLiteral("keyword.operator"), "op"},
      {QStringLiteral("keyword.return"), "kw"},
      {QStringLiteral("string"), "str"},
      {QStringLiteral("string.escape"), "str"},
      {QStringLiteral("number"), "num"},
      {QStringLiteral("number.float"), "num"},
      {QStringLiteral("comment"), "cmt"},
      {QStringLiteral("comment.documentation"), "cmt"},
      {QStringLiteral("type"), "ty"},
      {QStringLiteral("type.builtin"), "ty"},
      {QStringLiteral("function"), "fn"},
      {QStringLiteral("function.call"), "fn"},
      {QStringLiteral("function.method"), "fn"},
      {QStringLiteral("variable.parameter"), "var"},
      {QStringLiteral("operator"), "op"},
      {QStringLiteral("punctuation"), "op"},
      {QStringLiteral("constant"), "num"},
      {QStringLiteral("property"), "attr"},
  };
  if (const auto it = kExact.constFind(QString::fromStdString(cap));
      it != kExact.constEnd()) {
    return std::string(it.value());
  }
  // query 里的捕获名常带层级后缀（keyword.modifier、string.special…），
  // 按前缀归类兜底
  for (auto it = kExact.constBegin(); it != kExact.constEnd(); ++it) {
    if (cap.rfind(it.key().toStdString(), 0) == 0) {
      return std::string(it.value());
    }
  }
  return {};
}

std::string EscapeHtmlBytes(const std::string& text) {
  std::string out;
  out.reserve(text.size());
  for (const char c : text) {
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
        // hljs 时代的产物把撇号转成 &#x27;，markdown_to_html 的断言钉着
        // 这个形态；md4c 接续不受影响
        out += "&#x27;";
        break;
      default:
        out += c;
    }
  }
  return out;
}

// 单条谓词规则：#match? @cap "re" / #eq? @a (@b|"s") / #ne? / #any-*。
// 多条规则之间按 AND 求值
struct PredicateRule {
  enum Type { kMatch, kEq, kNeq, kAnyEq, kAnyNeq };
  Type type = kEq;
  uint32_t lhs = 0;
  bool rhs_is_capture = false;
  uint32_t rhs_capture = 0;
  QRegularExpression regex;  // kMatch 用
  QString text;              // kEq/kNeq 的字符串操作数
};

// 解析某 pattern 的谓词步骤。字符串表与捕获 id 都是 query 全局的，
// 求值阶段再取文本
QVector<PredicateRule> ParsePredicates(const TSQuery* query,
                                       uint32_t pattern_index) {
  uint32_t length = 0;
  const TSQueryPredicateStep* steps =
      ts_query_predicates_for_pattern(query, pattern_index, &length);
  QVector<PredicateRule> rules;
  PredicateRule current;
  bool has_type = false;
  bool has_lhs = false;
  bool has_rhs = false;
  for (uint32_t i = 0; i < length; ++i) {
    const TSQueryPredicateStep& step = steps[i];
    switch (step.type) {
      case TSQueryPredicateStepTypeDone:
        if (has_type && has_lhs) {
          rules.append(current);
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
        const QString text =
            QString::fromUtf8(value, static_cast<int>(string_length));
        // 首个 String step 是谓词名（#match? 等），后续是操作数
        if (!has_type) {
          if (text == QLatin1String("match?")) {
            current.type = PredicateRule::kMatch;
          } else if (text == QLatin1String("eq?")) {
            current.type = PredicateRule::kEq;
          } else if (text == QLatin1String("ne?")) {
            current.type = PredicateRule::kNeq;
          } else if (text == QLatin1String("any-eq?")) {
            current.type = PredicateRule::kAnyEq;
          } else if (text == QLatin1String("any-not-eq?")) {
            current.type = PredicateRule::kAnyNeq;
          } else {
            has_type = true;  // 未支持的谓词：不出规则，整条被丢弃
          }
          has_type = true;
        } else if (!has_rhs) {
          if (current.type == PredicateRule::kMatch) {
            current.regex = QRegularExpression(text);
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

// 只着色 variable 的参数形态；裸 variable（赋值目标、局部引用）不着色，
// 与 hljs 的输出密度对齐——全上色会糊成一片
struct LanguageInfo {
  const TSLanguage* language = nullptr;
  TSQuery* query = nullptr;
  std::vector<std::string> cap_classes;
  // pattern_index → 该 pattern 的谓词规则（#match?/#eq?/... 与 #
  // any-* 变体）。query cursor 不自动执行谓词，必须调用方求值
  QHash<uint32_t, QVector<PredicateRule>> predicates;
};

const char* const kScmPrefix = ":/markdown/ts/";
const char* const kScmSuffix = ".scm";

// 进程级只读注册表。TSQuery 创建后不可变，跨线程只读安全；首次调用在
// GUI 或 worker 侧都可能发生，qrc 读取与 ts_query_new 均线程安全
const QHash<QString, LanguageInfo>& Registry() {
  static const QHash<QString, LanguageInfo> registry = [] {
    // 幂等：单元测试直调高亮器时，主题路径可能还没初始化过 qrc
    EnsureHighlightResources();

    struct Entry {
      const char* name;
      const TSLanguage* (*language)(void);
      // cpp 的 highlights 沿用 c 的捕获并叠加自身条目（grammar 继承）
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

    QHash<QString, LanguageInfo> registry;
    for (const Entry& entry : entries) {
      // kScmPrefix/kScmSuffix 是常量指针而非字面量，不能过 QStringLiteral
      QFile query_file(QString::fromLatin1(kScmPrefix) + entry.name +
                       QString::fromLatin1(kScmSuffix));
      QByteArray scm;
      if (entry.inherit != nullptr) {
        QFile parent(QString::fromLatin1(kScmPrefix) + entry.inherit +
                     QString::fromLatin1(kScmSuffix));
        if (parent.open(QIODevice::ReadOnly)) {
          scm = parent.readAll();
          scm += '\n';
        }
      }
      if (!query_file.open(QIODevice::ReadOnly)) {
        qWarning("highlight: query resource missing for %s", entry.name);
        continue;
      }
      scm += query_file.readAll();

      const QString name = QString::fromLatin1(entry.name);
      uint32_t error_offset = 0;
      TSQueryError error = TSQueryErrorNone;
      TSQuery* query = ts_query_new(entry.language(), scm.constData(),
                                    static_cast<uint32_t>(scm.size()),
                                    &error_offset, &error);
      if (query == nullptr) {
        // query 编不过 = 该语言退回纯转义；不拖垮其它语言
        qWarning("highlight: query compile failed for %s at %u (error %d)",
                 entry.name, error_offset, static_cast<int>(error));
        continue;
      }
      LanguageInfo info;
      info.language = entry.language();
      info.query = query;
      info.cap_classes.resize(ts_query_capture_count(query));
      for (uint32_t i = 0; i < info.cap_classes.size(); ++i) {
        uint32_t length = 0;
        const char* capture = ts_query_capture_name_for_id(query, i, &length);
        info.cap_classes[i] = MapCapture(std::string(capture, length));
      }
      const uint32_t pattern_count = ts_query_pattern_count(query);
      for (uint32_t pattern = 0; pattern < pattern_count; ++pattern) {
        const QVector<PredicateRule> rules = ParsePredicates(query, pattern);
        if (!rules.isEmpty()) {
          info.predicates.insert(pattern, rules);
        }
      }
      registry.insert(name, std::move(info));
    }
    return registry;
  }();
  return registry;
}

// 别名：围栏标签 → 注册名。ts 按 javascript 着色（容错解析下可接受）
QString ResolveAlias(const QString& language) {
  static const QHash<QString, QString> kAliases = {
      {QStringLiteral("js"), QStringLiteral("javascript")},
      {QStringLiteral("ts"), QStringLiteral("javascript")},
      {QStringLiteral("py"), QStringLiteral("python")},
      {QStringLiteral("sh"), QStringLiteral("bash")},
      {QStringLiteral("shell"), QStringLiteral("bash")},
      {QStringLiteral("c++"), QStringLiteral("cpp")},
      {QStringLiteral("bat"), QStringLiteral("batch")},
      {QStringLiteral("cmd"), QStringLiteral("batch")},
  };
  return kAliases.value(language, language);
}

}  // namespace

// 求值某 match 的谓词规则。语义收紧到本仓 query 实际用到的子集：
// #match? 要求全部出现都命中正则；#eq?/#ne? 按"全部/任一"与官方 CLI
// 对齐；规则引用的捕获在 match 里没有出现时规则放行（不否决）
bool PredicatesSatisfied(const LanguageInfo& info, const TSQueryMatch& match,
                         const std::string& source) {
  const auto rules_it = info.predicates.constFind(match.pattern_index);
  if (rules_it == info.predicates.constEnd()) {
    return true;
  }
  auto text_of = [&](uint32_t capture_id, std::vector<QString>* out) {
    for (uint16_t i = 0; i < match.capture_count; ++i) {
      if (match.captures[i].index != capture_id) {
        continue;
      }
      const TSNode& node = match.captures[i].node;
      out->push_back(QString::fromUtf8(
          source.data() + ts_node_start_byte(node),
          static_cast<int>(ts_node_end_byte(node) - ts_node_start_byte(node))));
    }
  };
  auto regex_hit = [](const QString& text, const QRegularExpression& re) {
    return re.match(text).hasMatch();
  };

  for (const PredicateRule& rule : rules_it.value()) {
    std::vector<QString> lhs;
    text_of(rule.lhs, &lhs);
    if (lhs.empty()) {
      continue;
    }
    std::vector<QString> rhs;
    if (rule.rhs_is_capture) {
      text_of(rule.rhs_capture, &rhs);
    }
    const QString& rhs_text = rule.rhs_is_capture
                                  ? (rhs.empty() ? QString() : rhs.front())
                                  : rule.text;

    bool satisfied = false;
    switch (rule.type) {
      case PredicateRule::kMatch:
        satisfied = std::all_of(lhs.begin(), lhs.end(), [&](const QString& t) {
          return regex_hit(t, rule.regex);
        });
        break;
      case PredicateRule::kEq:
        satisfied = std::all_of(lhs.begin(), lhs.end(), [&](const QString& t) {
          return t == rhs_text;
        });
        break;
      case PredicateRule::kNeq:
        satisfied = std::none_of(lhs.begin(), lhs.end(), [&](const QString& t) {
          return t == rhs_text;
        });
        break;
      case PredicateRule::kAnyEq:
        satisfied = std::any_of(lhs.begin(), lhs.end(), [&](const QString& t) {
          return t == rhs_text;
        });
        break;
      case PredicateRule::kAnyNeq:
        satisfied = std::any_of(lhs.begin(), lhs.end(), [&](const QString& t) {
          return t != rhs_text;
        });
        break;
    }
    if (!satisfied) {
      return false;
    }
  }
  return true;
}

struct TreeSitterParserCache::Impl {
  // TSParser 不可跨线程：本缓存归属单个 worker/GUI 线程
  QHash<QString, TSParser*> parsers;
};

TreeSitterParserCache::TreeSitterParserCache() : impl_(new Impl) {}

TreeSitterParserCache::~TreeSitterParserCache() {
  for (TSParser* parser : std::as_const(impl_->parsers)) {
    ts_parser_delete(parser);
  }
  delete impl_;
}

QString TreeSitterHighlightHtml(const QString& code, const QString& language,
                                TreeSitterParserCache* cache) {
  if (code.isEmpty()) {
    return {};
  }
  const std::string source = code.toUtf8().toStdString();

  const QString registered = ResolveAlias(language);
  const auto info_it = Registry().constFind(registered);
  if (info_it == Registry().constEnd()) {
    // 白名单外：纯转义，不做语言猜测（猜错比不高亮更难看）
    return QString::fromStdString(EscapeHtmlBytes(source));
  }
  const LanguageInfo& info = info_it.value();

  // parser 按语言惰性建；cache 为空（仅测试）时一次性创建
  TSParser* parser = nullptr;
  std::unique_ptr<TSParser, void (*)(TSParser*)> temp_parser(
      nullptr, [](TSParser* p) { ts_parser_delete(p); });
  if (cache != nullptr) {
    auto& parsers = cache->impl_->parsers;
    const auto parser_it = parsers.constFind(registered);
    if (parser_it != parsers.constEnd()) {
      parser = parser_it.value();
    } else {
      parser = ts_parser_new();
      parsers.insert(registered, parser);
    }
  } else {
    temp_parser.reset(ts_parser_new());
    parser = temp_parser.get();
  }
  if (!ts_parser_set_language(parser, info.language)) {
    return QString::fromStdString(EscapeHtmlBytes(source));
  }

  TSTree* tree = ts_parser_parse_string(parser, nullptr, source.data(),
                                        static_cast<uint32_t>(source.size()));
  if (tree == nullptr) {
    return QString::fromStdString(EscapeHtmlBytes(source));
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
    if (!PredicatesSatisfied(info, match, source)) {
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

  // 同一字节被多层捕获覆盖时取最内层（起点更大/终点更小者）
  std::sort(captures.begin(), captures.end(),
            [](const CaptureRange& a, const CaptureRange& b) {
              if (a.start != b.start) {
                return a.start < b.start;
              }
              return a.end > b.end;
            });

  // 逐字节归入所属捕获；外层 capture 被内层覆写（vector 后写胜出）
  std::vector<int> class_map(source.size(), -1);
  for (size_t i = 0; i < captures.size(); ++i) {
    for (uint32_t j = captures[i].start; j < captures[i].end; ++j) {
      class_map[j] = static_cast<int>(i);
    }
  }

  // 契约：按 \n 切行后每行 span 自平衡——跨行捕获（多行注释/字符串）
  // 在换行处强制闭合，下一行开头重新开 span
  std::string out;
  out.reserve(source.size() * 2);
  std::string current_class;
  for (size_t i = 0; i < source.size(); ++i) {
    const char c = source[i];
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
        index != -1 ? *captures[static_cast<size_t>(index)].css : std::string();
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
        // 与 hljs 产物对齐（markdown_to_html 断言钉着 &#x27; 形态）
        out += "&#x27;";
        break;
      default:
        out += c;
    }
  }
  if (!current_class.empty()) {
    out += "</span>";
  }
  return QString::fromUtf8(out.data(), static_cast<int>(out.size()));
}
