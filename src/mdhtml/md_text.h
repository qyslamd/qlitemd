#ifndef FILE_MDHTML_MD_TEXT_H_
#define FILE_MDHTML_MD_TEXT_H_

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// UTF-8 helpers shared by the Qt-free core. All strings are UTF-8 bytes.
std::string_view TrimView(std::string_view text);
std::string Trim(std::string_view text);
bool StartsWith(std::string_view text, std::string_view prefix);
bool EndsWith(std::string_view text, std::string_view suffix);
std::vector<std::string> SplitLines(std::string_view text);
std::string ToLowerAscii(std::string_view text);

// Decodes one UTF-8 code point at byte offset. Returns false on invalid
// input; consumed is always at least one byte when called on non-empty input.
bool DecodeUtf8(std::string_view text, std::size_t offset, uint32_t* code_point,
                std::size_t* consumed);
void AppendUtf8(uint32_t code_point, std::string* out);
bool IsUnicodeSpace(uint32_t code_point);
bool IsUnicodeLetterOrNumber(uint32_t code_point);

// GitHub-flavoured heading slug: letters/numbers kept, whitespace -> '-',
// '-' and '_' kept, other punctuation dropped. CJK and other common
// Unicode letter ranges are preserved.
std::string Slugify(std::string_view utf8);
std::string EscapeHtml(std::string_view text);
std::string UnescapeHtml(std::string_view text);
std::string RandomHexSalt(std::size_t hex_chars);

#endif  // FILE_MDHTML_MD_TEXT_H_
