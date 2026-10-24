#ifndef FILE_MDHTML_MD_HIGHLIGHT_H_
#define FILE_MDHTML_MD_HIGHLIGHT_H_

#include <memory>
#include <string>
#include <string_view>

// Qt-free syntax highlighting: tree-sitter parse + bundled highlights query
// -> HTML spans carrying the short CSS class names used by the themes
// (kw/str/num/cmt/ty/fn/var/op/attr). The output contract matches the
// previous Qt module byte for byte: spans close at every newline, unknown
// languages are plain-escaped, and text is HTML-escaped.
//
// The language registry (TSLanguage/TSQuery) is process-wide and immutable
// after first use, so it is safe to read from any thread. TSParser is not
// thread-safe; each worker owns a MdTreeSitterParserCache.
//
// Bundled languages: bash/batch/c/cpp/css/go/html/java/javascript/json/python/
// rust (aliases js/ts/py/sh/shell/c++/bat/cmd). Unknown languages are
// escaped without language guessing.

class MdTreeSitterParserCache {
 public:
  MdTreeSitterParserCache();
  ~MdTreeSitterParserCache();
  MdTreeSitterParserCache(const MdTreeSitterParserCache&) = delete;
  MdTreeSitterParserCache& operator=(const MdTreeSitterParserCache&) = delete;
  MdTreeSitterParserCache(MdTreeSitterParserCache&&) = delete;
  MdTreeSitterParserCache& operator=(MdTreeSitterParserCache&&) = delete;

 private:
  friend std::string MdHighlightCode(std::string_view code,
                                     std::string_view language,
                                     MdTreeSitterParserCache* cache);
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

// Highlights code. Empty input, unknown languages and parser/query failures
// fall back to escaped source (a highlighting failure must not hide text).
// cache may be null for one-off use; production callers pass a per-thread
// cache so parsers are reused across code blocks.
std::string MdHighlightCode(std::string_view code, std::string_view language,
                            MdTreeSitterParserCache* cache);

// Stable dedupe key shared by the scheduler and render caches.
std::string MdHighlightRenderKey(std::string_view code,
                                 std::string_view language);

#endif  // FILE_MDHTML_MD_HIGHLIGHT_H_
