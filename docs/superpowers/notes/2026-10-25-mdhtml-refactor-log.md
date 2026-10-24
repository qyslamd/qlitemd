# mdhtml 重构实施日志

分支：`feature/qt-free-mdhtml`
日期：2026-10-25

## S1 核心骨架（完成）

- 新增 `src/mdhtml`，目标 `qlitemd_mdhtml`，只链接 md4c，不链接 Qt。
- 用 std::string/std::string_view/std::vector 重写：
  - md4c 解析与 UTF-8 输出；
  - `$…$` / `$$…$$` 抽取与占位符回填；
  - GFM 代码块识别、代码框/复制链接、可选行号；
  - Mermaid 围栏回调与失败回退；
  - 标题 slug、重复后缀、ASCII id、`data-md-slug`；
  - HTML 转义/反转义、Unicode 空白与常见文字范围、UTF-8 解码。
- 设计取舍：
  - 代码块/标题扫描改为手写扫描器，避免把 std::regex 带进核心；
  - 公式占位符索引手写解析，超长/非法索引不会抛异常；
  - slug 保留 ASCII 大小写折叠、CJK 与常见 Unicode 字母/数字范围，
    非 ASCII 大写暂不折叠（后续可补表）。
  - S1 还没有内置引擎；通过 `MdRenderResolver` 注入高亮/公式/
    Mermaid 结果，便于无 Qt 单测。
- 测试：`tests/mdhtml`，8/8 通过；未链接 Qt。
- 仍需：tree-sitter 高亮、公式/Mermaid SVG、字体度量、线程池、
  C API、Qt 适配层迁移。

## S2 语法高亮（完成）

- 新增 `md_highlight.{h,cpp}`：tree-sitter 解析 + highlights query
  → 带短类名 `<span>` 的 HTML，接口为
  `MdHighlightCode(code, language, cache)` 与
  `MdHighlightRenderKey(code, language)`。
- 12 种语言（bash/batch/c/cpp/css/go/html/java/javascript/json/
  python/rust）与别名表、`cpp` 继承 `c` 查询、逐语言
  `MdTreeSitterParserCache`，均与旧 Qt 模块一致。
- 查询资源不再走 qrc：`cmake/embed_mdhtml_queries.cmake` 在构建期
  把 `3rdparty/tree-sitter-lang/*/queries/highlights.scm` 生成为
  raw string 字面量的 C++ 源；核心不依赖 Qt 资源系统。
- 谓词求值保留旧语义（`#match?` 全部命中、`#eq?/#ne?` 全有/全无、
  缺少捕获则跳过规则）。`#is-not?` 仍按旧的等值规则处理，保证
  JavaScript 内建名的高亮结果与旧版逐字节一致。
- `std::regex` 不支持内联标志：查询里只出现行首 `(?i)`，编译时转成
  `icase`；其余语法均在 ECMAScript 子集内。
- 输出契约不变：按 `\n` 拆行后 span 自平衡；未注册语言纯转义；
  `'` 输出 `&#x27;`，与旧版及主题 CSS 契约一致。
- 测试：`tests/mdhtml/md_highlight_test.cpp`，10 项通过；连同 S1
  共 2 个测试目标、18 项通过，均未链接 Qt。
