#include "markdown_to_html.h"

#include <QRegularExpression>
#include <QSet>
#include <QStringList>
#include <QUuid>
#include <QVector>

#include "code_highlighter.h"
#include "heading_ids.h"
#include "html_escape.h"

extern "C" {
#include "md4c-html.h"
}

namespace {

// 公式在 md4c 眼里就是个普通文本占位符。抽出来在两头处理，是因为 $ 在两处
// 意思不同：代码块/行内代码里是字面量，正文里才是公式——只有源码阶段分得清，
// 对成品 HTML 正则会把 `awk '{ print $1 }'` 也当公式
//
// 占位符带每次调用的随机盐：占位符要在整份成品 HTML 上全局换回，固定格式
// （如 @@mathjax-0@@）会被用户文本/代码块里的同形字面量撞上，被错换成公式
QString MathPlaceholder(const QString& salt, int index) {
  return QStringLiteral("@@mj-%1-%2@@").arg(salt).arg(index);
}

// 这个位置上的 $ 是不是被反斜杠转义了。数前面连续的反斜杠：奇数个才算转义
// （\\$ 是"转义的反斜杠 + 定界符"）。找闭标记时也要用它——只看主循环当前位置
// 会漏掉"闭标记落在 \$ 上"的情形（实测 bug：$5 到 $10 之间，转义 \$y\$ 被判成
// 一整段公式）
bool IsEscapedAt(const QString& line, int index) {
  int backslashes = 0;
  for (int i = index - 1; i >= 0 && line.at(i) == QLatin1Char('\\'); --i) {
    ++backslashes;
  }
  return backslashes % 2 == 1;
}

// 处理一行：跳过行内代码与转义的 \$，剩下的 $…$ / $$…$$ 换成占位符
QString ExtractMathInLine(const QString& line,
                          QVector<MarkdownMathSegment>* segments,
                          const QString& salt) {
  // 占位符比原文短、换行不变：按原长留底，逐段 append 不会反复扩容
  QString out;
  out.reserve(line.size());
  int i = 0;
  const int size = line.size();
  while (i < size) {
    const QChar current = line.at(i);

    // 行内代码：整段照抄，里面的 $ 不动
    if (current == QLatin1Char('`')) {
      int run = 1;
      while (i + run < size && line.at(i + run) == QLatin1Char('`')) {
        ++run;
      }
      const int close = line.indexOf(QString(run, QLatin1Char('`')), i + run);
      if (close < 0) {  // 没有配对的收尾，按普通文本处理
        out.append(line.constData() + i, size - i);
        break;
      }
      out.append(line.constData() + i, close + run - i);
      i = close + run;
      continue;
    }

    if (current != QLatin1Char('$')) {
      out += current;
      ++i;
      continue;
    }

    // 开标记与闭标记用同一条转义规则：数前面连续反斜杠的奇偶。被转义的 $
    // 原样输出（前面的反斜杠已按普通字符输出），md4c 会渲染成字面 $
    if (IsEscapedAt(line, i)) {
      out += current;
      ++i;
      continue;
    }

    // $$…$$：独立公式
    if (i + 1 < size && line.at(i + 1) == QLatin1Char('$')) {
      // 跳过 \$$ 这种转义的位置：indexOf 直接取第一个命中会把公式
      // 截在 \$$ 上（$…$ 分支有 IsEscapedAt，这条曾经漏了）
      int close = -1;
      for (int scan = line.indexOf(QStringLiteral("$$"), i + 2); scan >= 0;
           scan = line.indexOf(QStringLiteral("$$"), scan + 1)) {
        if (!IsEscapedAt(line, scan)) {
          close = scan;
          break;
        }
      }
      if (close > i + 2) {
        segments->append({line.mid(i + 2, close - i - 2), true});
        out += MathPlaceholder(salt, segments->size() - 1);
        i = close + 2;
        continue;
      }
      out += QStringLiteral("$$");
      i += 2;
      continue;
    }

    // $…$：行内公式。开标记后不能是空白，闭标记前不能是空白、后面不能跟数字
    // ——这两条是为了不和"价格在 $5 到 $10 之间"这类正文打架
    int close = -1;
    if (i + 1 < size && !line.at(i + 1).isSpace()) {
      for (int j = i + 1; j < size; ++j) {
        if (line.at(j) != QLatin1Char('$') || line.at(j - 1).isSpace() ||
            IsEscapedAt(line, j)) {
          continue;
        }
        if (j + 1 < size && line.at(j + 1).isDigit()) {
          break;  // "$5 … $10"：不配对，放弃
        }
        close = j;
        break;
      }
    }
    if (close > i + 1) {
      segments->append({line.mid(i + 1, close - i - 1), false});
      out += MathPlaceholder(salt, segments->size() - 1);
      i = close + 1;
      continue;
    }

    out += current;
    ++i;
  }
  return out;
}

// ATX 标题：1~6 个 #，后面是空白或行尾
bool IsAtxHeading(const QString& trimmed) {
  int hashes = 0;
  while (hashes < trimmed.size() && trimmed.at(hashes) == QLatin1Char('#')) {
    ++hashes;
  }
  if (hashes < 1 || hashes > 6) {
    return false;
  }
  return hashes == trimmed.size() || trimmed.at(hashes).isSpace();
}

// 分隔线：同一种标记（* - _）至少 3 个，中间可以有空白
bool IsThematicBreak(const QString& trimmed) {
  if (trimmed.isEmpty()) {
    return false;
  }
  const QChar marker = trimmed.at(0);
  if (marker != QLatin1Char('*') && marker != QLatin1Char('-') &&
      marker != QLatin1Char('_')) {
    return false;
  }
  int count = 0;
  for (const QChar c : trimmed) {
    if (c == marker) {
      ++count;
    } else if (!c.isSpace()) {
      return false;
    }
  }
  return count >= 3;
}

// Setext 下划线：整行只有 = 或 -。只在前一行是段落时才算块标记——
// 空行后的 "=====" 在 CommonMark 里只是一段普通正文
bool IsSetextUnderline(const QString& trimmed) {
  if (trimmed.isEmpty()) {
    return false;
  }
  const QChar marker = trimmed.at(0);
  if (marker != QLatin1Char('=') && marker != QLatin1Char('-')) {
    return false;
  }
  for (const QChar c : trimmed) {
    if (c != marker) {
      return false;
    }
  }
  return true;
}

// 该行是否以引用标记开头；是则把标记剥掉，剩下的内容写进 rest
bool StripBlockQuoteMarker(const QString& trimmed, QString* rest) {
  if (!trimmed.startsWith(QLatin1Char('>'))) {
    return false;
  }
  int i = 0;
  while (i < trimmed.size() && trimmed.at(i) == QLatin1Char('>')) {
    ++i;
    if (i < trimmed.size() && trimmed.at(i).isSpace()) {
      ++i;
    }
  }
  *rest = trimmed.mid(i);
  return true;
}

// 这一行是否结束了当前段落（后面的 4 空格行应进缩进代码块）。
// 列表项不算：CommonMark 的 lazy continuation 让 "- item" 后的缩进行
// 仍是列表内容。引用行只有"空引用"或引用内是完整块时才结束段落，
// "> quote" 后的缩进行同样是 lazy continuation
bool EndsParagraph(const QString& trimmed, bool paragraph_open) {
  if (IsAtxHeading(trimmed) || IsThematicBreak(trimmed)) {
    return true;
  }
  if (paragraph_open && IsSetextUnderline(trimmed)) {
    return true;
  }
  QString quote_rest;
  if (StripBlockQuoteMarker(trimmed, &quote_rest)) {
    const QString inner = quote_rest.trimmed();
    return inner.isEmpty() || IsAtxHeading(inner) || IsThematicBreak(inner) ||
           (paragraph_open && IsSetextUnderline(inner));
  }
  return false;
}

// 逐行抽取：围栏代码块整段跳过；单独成行的 $$ 会把中间的行合并成一段公式
QString ExtractMath(const QString& markdown,
                    QVector<MarkdownMathSegment>* segments,
                    const QString& salt) {
  const QStringList lines = markdown.split(QLatin1Char('\n'));
  QStringList result;
  result.reserve(lines.size());

  bool in_fence = false;
  QChar fence_open_char;
  int fence_open_len = 0;
  QString display_block;
  bool in_display_block = false;
  // 缩进代码块（CommonMark：4 空格或 tab，且不能打断段落——段落开着的时候
  // 缩进行只是续行）。近似口径：段落态按"上一非围栏/非代码行是否非空"维护，
  // 列表项内的深缩进场景不覆盖
  bool in_indent_code = false;
  bool paragraph_open = false;

  for (const QString& line : lines) {
    const QString trimmed = line.trimmed();
    const bool blank = trimmed.isEmpty();
    // 缩进的围栏行（4 空格起头的 ```）不是围栏：在缩进代码块里是内容，
    // 在围栏里也不构成闭合（CommonMark 的闭合围栏不允许 4 空格缩进）
    const bool indented = line.startsWith(QLatin1String("    ")) ||
                          line.startsWith(QLatin1Char('\t'));

    // 缩进代码块（CommonMark：4 空格或 tab，且不能打断段落——段落开着的
    // 时候缩进行只是续行）。近似口径：段落态按"上一非围栏/非代码行是否
    // 非空"维护，列表项内的深缩进场景不覆盖
    if (!in_display_block && !in_fence && !in_indent_code && indented &&
        !paragraph_open) {
      in_indent_code = true;
    }
    if (in_indent_code) {
      if (indented || blank) {
        result << line;
        paragraph_open = false;
        continue;
      }
      // 非缩进非空：代码块到此为止。这行本身可能是围栏（缩进代码块后跟
      // 围栏极常见），不能吞掉围栏判定——落下去继续走
      in_indent_code = false;
    }

    // 围栏行：3 个以上同字符（` 或 ~）打头。开围栏后面可以跟 info
    // string；闭合围栏要求同字符、长度不短于开启、且后面只剩空白
    // （CommonMark）——只比前 3 个字符的话，```` 开的块会被 ``` 提前
    // 收掉，后面的 $ 抽取全乱
    QChar fence_char;
    int fence_run = 0;
    if (!trimmed.isEmpty() && (trimmed.at(0) == QLatin1Char('`') ||
                               trimmed.at(0) == QLatin1Char('~'))) {
      fence_char = trimmed.at(0);
      while (fence_run < trimmed.size() &&
             trimmed.at(fence_run) == fence_char) {
        ++fence_run;
      }
    }
    if (!in_display_block && !indented && fence_run >= 3) {
      if (!in_fence) {
        in_fence = true;
        fence_open_char = fence_char;
        fence_open_len = fence_run;
      } else if (fence_char == fence_open_char && fence_run >= fence_open_len &&
                 trimmed.mid(fence_run).trimmed().isEmpty()) {
        in_fence = false;
      }
      result << line;
      paragraph_open = false;  // 围栏行不是段落内容
      continue;
    }
    if (in_fence) {
      result << line;
      continue;
    }

    if (in_display_block) {
      if (trimmed == QLatin1String("$$")) {
        segments->append({display_block, true});
        in_display_block = false;
        display_block.clear();
        result << MathPlaceholder(salt, segments->size() - 1);
        paragraph_open = false;
      } else {
        display_block += line + QLatin1Char('\n');
      }
      continue;
    }
    if (trimmed == QLatin1String("$$")) {
      in_display_block = true;
      display_block.clear();
      paragraph_open = false;
      continue;
    }

    result << ExtractMathInLine(line, segments, salt);
    // 标题/分隔线/空引用之后是完整块，不能再把下一行当段落续行——否则
    // 后面的缩进代码块会被跳过判定，块里的 $ 被误当公式抽走
    paragraph_open = !blank && !EndsParagraph(trimmed, paragraph_open);
  }

  // 没闭合的块级公式：按原文吐回去，不当公式
  if (in_display_block) {
    result << QStringLiteral("$$");
    result << display_block.trimmed();
    result << QStringLiteral("$$");
  }
  return result.join(QLatin1Char('\n'));
}

// 占位符换回渲染结果。渲染不出来（或调用方没给回调）就显示原文——一条坏公式
// 不该吞掉整页
QString RestoreMath(const QString& html,
                    const QVector<MarkdownMathSegment>& segments,
                    const FormulaSink& formula, const QString& salt) {
  // 盐是十六进制串，对正则安全
  const QRegularExpression kSlot(QStringLiteral("@@mj-%1-(\\d+)@@").arg(salt));
  // 渲染结果通常比占位符长，按原文留底已经省掉绝大多数扩容
  QString result;
  result.reserve(html.size());
  int consumed = 0;
  auto matches = kSlot.globalMatch(html);
  while (matches.hasNext()) {
    const QRegularExpressionMatch match = matches.next();
    result.append(html.constData() + consumed,
                  match.capturedStart() - consumed);
    consumed = match.capturedEnd();

    const int index = match.captured(1).toInt();
    if (index < 0 || index >= segments.size()) {
      continue;
    }
    const MarkdownMathSegment& segment = segments.at(index);
    const QString rendered =
        formula ? formula(segment.tex, segment.display) : QString();
    if (!rendered.isEmpty()) {
      result += rendered;
      continue;
    }
    const QString source =
        segment.display
            ? QStringLiteral("$$") + segment.tex + QStringLiteral("$$")
            : QStringLiteral("$") + segment.tex + QStringLiteral("$");
    result += QStringLiteral("<code class=\"math-src\">%1</code>")
                  .arg(source.toHtmlEscaped());
  }
  result.append(html.constData() + consumed, html.size() - consumed);
  return result;
}

void AppendHtml(const MD_CHAR* text, MD_SIZE size, void* userdata) {
  static_cast<QString*>(userdata)->append(
      QString::fromUtf8(text, static_cast<int>(size)));
}

// 把 <pre><code class="language-xxx">…</code></pre> 里的代码交给高亮器。
//
// 两个约束都是为了躲开"原始 HTML 直通"：md4c 在 GFM 下放行内联原始 HTML，
// 正文里若出现没闭合的裸 <pre><code>，不加行首锚点就会从那处一直匹配到文档
// 后面下一个 </code></pre>，把中间整段正文吞成"代码"。md4c 生成的块级代码块
// 永远从行首开始，锚 ^ 之后只认真块（正则用 Multiline 让 ^ 匹配每行行首）
// 代码块包一层带 header 的框：左边语言标签、右边"复制"链接。复制链接是
// copy:<id>，id 交给调用方去查原文（见 CodeBlockSink）——这里只负责把 id 写
// 进 HTML，不碰剪贴板（渲染层不该知道宿主用不用 Qt 剪贴板）
// 高亮载荷已在 JS 侧逐行闭合/重开 span（highlight_entry.js 的
// splitSpansPerLine，code_highlighter_test 钉住），这里按换行直切即可。
// 空块零行；"\n" 是一个空行；末尾一个换行不多出一行
QStringList SplitCodeLines(const QString& highlighted) {
  if (highlighted.isEmpty()) {
    return {};
  }
  QString body = highlighted;
  if (body.endsWith(QLatin1Char('\n'))) {
    body.chop(1);
  }
  return body.split(QLatin1Char('\n'));
}

QString BuildCodeBlockFrame(const QString& highlighted, const QString& language,
                            const QString& block_id, bool line_numbers) {
  const QString lang_text =
      language.isEmpty() ? QStringLiteral("text") : language;
  QString out = QStringLiteral("<div class=\"code-wrap\">");
  out += QStringLiteral("<div class=\"code-header\">");
  out += QStringLiteral("<span class=\"code-lang\">%1</span>")
             .arg(lang_text.toHtmlEscaped());
  out += QStringLiteral("<a class=\"copy-btn\" href=\"copy:%1\">复制</a>")
             .arg(block_id);
  out += QStringLiteral("</div>");
  if (line_numbers) {
    out += QStringLiteral("<table class=\"md-code\"><tbody>");
    const QStringList lines = SplitCodeLines(highlighted);
    for (int i = 0; i < lines.size(); ++i) {
      out += QStringLiteral("<tr><td class=\"md-code-ln\">%1</td>").arg(i + 1);
      out += QStringLiteral("<td class=\"md-code-line\">");
      // 空行也要占一行高，否则整个 tr 可能塌掉
      out += lines.at(i).isEmpty() ? QStringLiteral("&#160;") : lines.at(i);
      out += QStringLiteral("</td></tr>");
    }
    out += QStringLiteral("</tbody></table>");
  } else if (language.isEmpty()) {
    // 无语言的围栏不写 language- 类：那是"代码块声明了哪种语言"的契约，
    // header 上显示 text 只是给人看的
    out += QStringLiteral("<pre><code>%1</code></pre>").arg(highlighted);
  } else {
    out += QStringLiteral("<pre><code class=\"language-%1\">%2</code></pre>")
               .arg(language.toHtmlEscaped(), highlighted);
  }
  out += QStringLiteral("</div>");
  return out;
}

// 解析阶段只收集代码块元数据，不做高亮/出图；索引按出现顺序编号
QVector<MarkdownCodeBlock> CollectCodeBlocks(const QString& html) {
  static const QRegularExpression kPreCode(
      QStringLiteral("^(<pre><code([^>]*)>)([\\s\\S]*?)(</code></pre>)"),
      QRegularExpression::MultilineOption);
  static const QRegularExpression kLanguage(
      QStringLiteral("class=\"language-([^\"]+)\""));

  QVector<MarkdownCodeBlock> blocks;
  int index = 0;
  auto matches = kPreCode.globalMatch(html);
  while (matches.hasNext()) {
    const QRegularExpressionMatch match = matches.next();
    MarkdownCodeBlock block;
    block.index = index++;
    const QRegularExpressionMatch found = kLanguage.match(match.captured(2));
    if (found.hasMatch()) {
      block.language = found.captured(1);
    }
    block.code = UnescapeHtml(match.captured(3));
    blocks.append(block);
  }
  return blocks;
}

QString HighlightCodeBlocks(const QString& html, const MermaidSink& mermaid,
                            const CodeBlockSink& code_blocks,
                            const MarkdownRenderOptions& options) {
  static const QRegularExpression kPreCode(
      QStringLiteral("^(<pre><code([^>]*)>)([\\s\\S]*?)(</code></pre>)"),
      QRegularExpression::MultilineOption);
  static const QRegularExpression kLanguage(
      QStringLiteral("class=\"language-([^\"]+)\""));

  // 代码块会被包上 header 整体变长：按原文留底，剩下的扩容只发生在块边界
  QString result;
  result.reserve(html.size());
  int consumed = 0;
  int block_index = 0;  // 每个代码块一个 id，与 copy: 链接里的编号对应
  auto matches = kPreCode.globalMatch(html);
  while (matches.hasNext()) {
    const QRegularExpressionMatch match = matches.next();
    result.append(html.constData() + consumed,
                  match.capturedStart() - consumed);
    consumed = match.capturedEnd();

    QString language;
    const QRegularExpressionMatch found = kLanguage.match(match.captured(2));
    if (found.hasMatch()) {
      language = found.captured(1);
    }

    // 代码里的 "<" 从 md4c 出来是实体，正文里不会出现字面的 "</code></pre>"，
    // 非贪婪匹配到这里就是代码结束
    const QString code = UnescapeHtml(match.captured(3));

    // mermaid 围栏不出代码块，交给 sink 出图（sink 拿的是实体已还原的源码）；
    // 渲染不出来再退回代码块，不留白
    if (mermaid && language == QLatin1String("mermaid")) {
      const QString image = mermaid(code);
      if (!image.isEmpty()) {
        result +=
            QStringLiteral("<div class=\"mermaid-wrap\">%1</div>").arg(image);
        continue;
      }
    }

    const QString block_id = QString::number(block_index++);
    if (code_blocks) {
      code_blocks(block_id, code);
    }
    const QString highlighted = options.highlight
                                    ? options.highlight(code, language)
                                    : HighlightCode(code, language);
    result += BuildCodeBlockFrame(highlighted, language, block_id,
                                  options.code_line_numbers);
  }
  result.append(html.constData() + consumed, html.size() - consumed);
  return result;
}

QString Slugify(const QString& text) {
  QString out;
  out.reserve(text.size());
  for (const QChar c : text) {
    if (c.isLetterOrNumber()) {
      out += c.toLower();
    } else if (c.isSpace()) {
      out += QLatin1Char('-');
    } else if (c == QLatin1Char('-') || c == QLatin1Char('_')) {
      out += c;
    }
  }
  return out;
}

}  // namespace

QString EscapeHtml(const QString& text) {
  QString out = text;
  out.replace(QChar('&'), QStringLiteral("&amp;"));
  out.replace(QChar('<'), QStringLiteral("&lt;"));
  out.replace(QChar('>'), QStringLiteral("&gt;"));
  out.replace(QChar('"'), QStringLiteral("&quot;"));
  return out;
}

QString UnescapeHtml(const QString& text) {
  QString out = text;
  out.replace(QStringLiteral("&quot;"), QStringLiteral("\""));
  out.replace(QStringLiteral("&lt;"), QStringLiteral("<"));
  out.replace(QStringLiteral("&gt;"), QStringLiteral(">"));
  out.replace(QStringLiteral("&#39;"), QStringLiteral("'"));
  out.replace(QStringLiteral("&apos;"), QStringLiteral("'"));
  // &amp; 必须最后：先解它会把 "&amp;lt;" 变成 "&lt;"，再被上一条二次解码
  out.replace(QStringLiteral("&amp;"), QStringLiteral("&"));
  return out;
}

QString AddHeadingIds(
    const QString& html, const HeadingSink& on_heading,
    const std::function<void(const MarkdownHeading&)>& on_outline) {
  static const QRegularExpression kHeading(
      QStringLiteral("<h([1-6])>(.*?)</h\\1>"),
      QRegularExpression::DotMatchesEverythingOption);
  static const QRegularExpression kTag(QStringLiteral("<[^>]*>"));

  // 标题标记只会让输出更长：先按原长留底，避免逐段 append 反复扩容
  QString out;
  out.reserve(html.size());
  int pos = 0;
  int index = 0;  // 全局序号：重名 slug 靠它拿到彼此不同的 id
  // 已分配给任何标题的 slug。只按"原始 slug 出现次数"加后缀会和显式
  // 同名的标题撞车：Foo / Foo-1 / Foo 的第三个会生成 foo-1
  QSet<QString> used;
  auto it = kHeading.globalMatch(html);
  while (it.hasNext()) {
    const auto match = it.next();
    out.append(html.constData() + pos, match.capturedStart() - pos);
    pos = match.capturedEnd();

    const QString level = match.captured(1);
    const QString inner = match.captured(2);

    // slug 用纯文本算：<h2>Hello <code>world</code></h2> 的锚点是
    // #hello-world，不把标签算进去
    QString plain = inner;
    plain.remove(kTag);
    plain = UnescapeHtml(plain);

    QString slug = Slugify(plain);
    const QString base = slug;
    int n = 0;
    while (used.contains(slug)) {
      ++n;
      slug = base + QStringLiteral("-%1").arg(n);
    }
    used.insert(slug);

    // id 必须是纯 ASCII：litehtml 解析时会改写 id 的存储形式，UTF-8 多字节
    // 经不起这一趟，非 ASCII 的 id 会让 scrollToAnchor 静默找不到元素。
    // 原 slug 存进 data-md-slug，宿主点击时拿它反查 id
    const QString ascii_id = QStringLiteral("h-%1").arg(index++);
    if (on_heading) {
      on_heading(slug, ascii_id);
    }
    if (on_outline) {
      MarkdownHeading heading;
      heading.level = level.toInt();
      heading.text = plain;
      heading.anchor = slug;
      on_outline(heading);
    }

    out += QStringLiteral("<h%1 id=\"%2\" data-md-slug=\"%3\">%4</h%1>")
               .arg(level, ascii_id, slug.toHtmlEscaped(), inner);
  }
  out.append(html.constData() + pos, html.size() - pos);
  return out;
}

MarkdownParseResult ParseMarkdown(const QString& markdown, bool extract_math) {
  MarkdownParseResult parsed;
  // 盐无条件生成：即使不抽公式，RestoreMath 也会用这个盐扫描；空盐会让
  // 正则退化成匹配用户正文里的字面占位符
  parsed.math_salt = QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
  QString source = markdown;
  if (extract_math) {
    source = ExtractMath(markdown, &parsed.segments, parsed.math_salt);
  }

  const QByteArray utf8 = source.toUtf8();
  // SKIP_UTF8_BOM：本模块的调用方给的是已解码的 QString（BOM 早被 Qt 吃掉），
  // 但直接喂原始字节的调用方会因 BOM 把首个块解析成正文，一个 flag 免掉这个坑
  const int result =
      md_html(utf8.constData(), static_cast<MD_SIZE>(utf8.size()), AppendHtml,
              &parsed.html, MD_DIALECT_GITHUB, MD_HTML_FLAG_SKIP_UTF8_BOM);
  if (result != 0) {
    parsed.html = QStringLiteral("<p class=\"error\">Markdown 解析失败</p>");
    parsed.parse_error = true;
    return parsed;
  }
  parsed.blocks = CollectCodeBlocks(parsed.html);
  return parsed;
}

QString AssembleMarkdown(const MarkdownParseResult& parsed,
                         const MarkdownRenderResolver& resolver,
                         const CodeBlockSink& code_blocks,
                         const HeadingSink& heading_ids,
                         const MarkdownRenderOptions& options) {
  if (parsed.parse_error) {
    return parsed.html;
  }
  // 组装层只查结果，不触发渲染；空结果由 HighlightCodeBlocks/RestoreMath
  // 按原逻辑回退到代码块或公式原文
  MarkdownRenderOptions resolved_options = options;
  resolved_options.highlight = [&resolver](const QString& code,
                                           const QString& language) {
    const QString rendered =
        resolver(MarkdownRenderKind::Highlight, code, language, false);
    // 取消/缺失结果不能把代码块变成空白；高亮返回空时退纯转义
    if (rendered.isEmpty() && !code.isEmpty()) {
      return EscapeHtml(code);
    }
    return rendered;
  };
  const MermaidSink mermaid = [&resolver](const QString& code) {
    return resolver(MarkdownRenderKind::Mermaid, code, QString(), false);
  };
  const FormulaSink formula = [&resolver](const QString& tex, bool display) {
    return resolver(MarkdownRenderKind::Formula, tex, QString(), display);
  };
  // 补标题锚点放在最后：前面的高亮/公式回填只会替换标签内部，不动 h1–h6
  // 的外框，这里的 id 因此不会被谁冲掉
  return AddHeadingIds(
      RestoreMath(HighlightCodeBlocks(parsed.html, mermaid, code_blocks,
                                      resolved_options),
                  parsed.segments, formula, parsed.math_salt),
      heading_ids, options.outline);
}

QString MarkdownToHtml(const QString& markdown, const FormulaSink& formula,
                       const MermaidSink& mermaid,
                       const CodeBlockSink& code_blocks,
                       const HeadingSink& heading_ids,
                       const MarkdownRenderOptions& options) {
  const MarkdownParseResult parsed =
      ParseMarkdown(markdown, static_cast<bool>(formula));
  if (parsed.parse_error) {
    return parsed.html;
  }
  const MarkdownRenderResolver resolver =
      [&formula, &mermaid, &options](MarkdownRenderKind kind,
                                     const QString& payload,
                                     const QString& language, bool display) {
        switch (kind) {
          case MarkdownRenderKind::Formula:
            return formula ? formula(payload, display) : QString();
          case MarkdownRenderKind::Mermaid:
            return mermaid ? mermaid(payload) : QString();
          case MarkdownRenderKind::Highlight:
            return options.highlight ? options.highlight(payload, language)
                                     : HighlightCode(payload, language);
        }
        return QString();
      };
  return AssembleMarkdown(parsed, resolver, code_blocks, heading_ids, options);
}
