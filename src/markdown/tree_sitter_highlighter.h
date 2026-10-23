#ifndef FILE_MARKDOWN_TREE_SITTER_HIGHLIGHTER_H_
#define FILE_MARKDOWN_TREE_SITTER_HIGHLIGHTER_H_

#include <QString>

class TreeSitterParserCache;

// 语法高亮核心：tree-sitter 解析 + highlights query 捕获 → 带 .kw/.str 等
// 短类名的 <span> HTML。替换原 QuickJS 里的 highlight.js；主题 CSS 契约
// （类名集合）与输出契约（按 \n 切分 span、未知语言纯转义、HTML 转义）
// 与 hljs 时代一致。
//
// 语言注册表进程级只读共享（TSLanguage/TSQuery 创建后不可变，可跨线程
// 读）；TSParser 不可跨线程，由调用方持 TreeSitterParserCache——异步
// worker 各持一份，GUI 同步入口用线程局部状态的缓存。
//
// 语言白名单：bash/batch/c/cpp/css/go/html/java/javascript/json/python/rust
// （别名 js/ts/py/sh/shell/c++/bat/cmd）。未注册语言返回纯转义原文，
// 不做自动语言猜测。

// 每引擎状态一份：按语言惰性创建 TSParser。不可跨线程共享。
class TreeSitterParserCache {
 public:
  TreeSitterParserCache();
  ~TreeSitterParserCache();
  TreeSitterParserCache(const TreeSitterParserCache&) = delete;
  TreeSitterParserCache& operator=(const TreeSitterParserCache&) = delete;

 private:
  friend QString TreeSitterHighlightHtml(const QString& code,
                                         const QString& language,
                                         TreeSitterParserCache* cache);
  struct Impl;
  Impl* impl_;
};

// 高亮 code。未注册语言、空输入返回纯转义；解析/查询失败同样回退纯
// 转义（高亮失败不该让正文消失）。cache 为 nullptr 时用一次性 parser
// （仅测试用；生产路径必须传缓存，否则每块都重建 parser）。
QString TreeSitterHighlightHtml(const QString& code, const QString& language,
                                TreeSitterParserCache* cache);

#endif  // FILE_MARKDOWN_TREE_SITTER_HIGHLIGHTER_H_
