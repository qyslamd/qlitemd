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

## S3 SVG 引擎（native 部分完成，QuickJS 待补）

- 新增 `md_svg`：根元素提取（8 MiB 上限）、viewBox/px 尺寸解析、
  公式颜色注入（currentColor / 根 `<g>` fill），只做 `kPortable`
  profile；QtSvg 兼容处理留在适配层。
- 新增 `md_hash`：自带 SHA-256；`MdAssetId` 取 kind+key 的
  SHA-256 前 128 位，资产 id 跨进程稳定。
- 新增 `md_font_metrics`：`MdFontMetrics` 注入接口与确定性的
  `MdDefaultFontMetrics`（Latin 分类字宽、CJK/全角 1em、组合字符 0）。
  额外增加 `Fingerprint()`，作为 Mermaid 缓存 key 的字体盐。
- 新增 `md_formula`：RaTeX FFI 输出 `<svg>`，字体 16px、padding 0、
  stroke 0.6，与旧管线一致；尺寸取 viewBox。
- 新增 `md_mermaid`：merman 原生输出 `<svg>`，light/dark 各一个
  engine 句柄；文本度量回调改接 `MdFontMetrics`，实现单行与按词换行；
  不再做 PNG/2x 采样/QtSvg 后处理。
- CMake 按后端裁剪：只有 `QLITEMD_FORMULA_BACKEND=ratex` 时链接
  `qlitemd::ratex`，只有 `mermaid` 时链接 `qlitemd::merman`；对应
  宏 `QLITEMD_MDHTML_FORMULA_RATEX` / `QLITEMD_MDHTML_MERMAID_MERMAN`
  PUBLIC 给测试使用。
- 本地验证的后端是 ratex + merman；QuickJS 路径（公式 MathJax、
  Mermaid JS bridge）在 Windows 构建里不编译，放到 S3b 用 WSL/Linux
  构建验证后再提交，避免写无法编译的代码。
- 测试：`test_mdhtml_svg`（11）、`test_mdhtml_font_metrics`（6）、
  `test_mdhtml_formula`（4）、`test_mdhtml_mermaid`（3）；
  MARKDOWN 全量 36/36 通过。
