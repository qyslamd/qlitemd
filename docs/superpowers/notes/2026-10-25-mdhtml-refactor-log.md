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

## S3b QuickJS 回退（完成）

- 新增 `md_js_runtime`：`js_runtime` 的 std 化版本，保留字节码加载、
  一次性失败、首次加载耗时与中断旗标；不打印日志。
- 新增 `md_bytecode.h` 与 `src/mdhtml/CMakeLists.txt` 里的 qjsc 规则：
  四份 JS 载荷按构建目录生成字节码并编进核心；仅在
  `QLITEMD_NEEDS_QUICKJS` 时参与构建。
- 公式 QuickJS 路径：MathJax `MathJaxAdapter.convert` → `<svg>`；
  颜色注入 currentColor，尺寸优先解析 `ex`（×8），退回 viewBox
  （×16/1000），与旧管线一致。
- Mermaid QuickJS 路径：移植 dom_shim bridge（`__measureText`、
  `__measureTextFull`、`__setSettled`）、`htmlLabels:false` 初始化、
  `%%{init}%%` 主题指令、Promise 微任务泵（20 万 job / 30 秒上限）。
  度量回调全部走 `MdFontMetrics`。
- 顶层 CMake 调整：`src/mdhtml` 移到 QuickJS/tree-sitter 之后，保证
  qjsc 与 tree-sitter 目标已存在。
- 验证：新增第二个构建目录 `build_qt515_quickjs_x86`（Qt 5.15.2 x86，
  formula/mermaid 都选 quickjs，关闭 md_viewer），6 个 mdhtml 测试
  目标全部通过；原 resvg+ratex+merman 构建的 MARKDOWN 36/36 仍通过。

## S4 多线程调度与 C API（完成）

- 新增 `md_pipeline`：
  - 每个 job 一个 coordinator 线程（解析、收集、等待、组装）；
  - 三条常驻 lane（highlight/formula/mermaid），每个 worker 持有自己的
    parser/引擎状态；Windows 用 `_beginthreadex` 给 32MB 栈；
  - 任务按稳定 key 去重；结果表 `(kind,key)`，组装按源码顺序查表，
    worker 完成顺序不影响 HTML；
  - 有界队列（默认 256）对 coordinator 施加背压；
  - 进度回调在 coordinator 线程执行，`completed/total/in_flight` 单调；
  - 取消：排队任务出队即丢弃，QuickJS 通过 interrupt handler 中断；
    原生 merman/RaTeX 当前完成手上的图。
  - 设计取舍：字体度量改为 pipeline 级属性（Mermaid worker 的
    merman 引擎在创建时绑定度量）；worker 数量也放在
    `MdPipelineOptions`，`MdRenderOptions` 只保留每次渲染的差异项。
- 资产 HTML：`<img src="qlitemd-asset:<id>" width=... height=...>`，
  公式显示态带 `class="math-display"`、Mermaid 带 `class="mermaid"`；
  `id` 是 SHA-256 前 128 位。
- 新增 `mdhtml_c.h` 同步 C API：pipeline/result 句柄、选项与 stats
  的 `struct_size` 版本化、资产/大纲访问器、字体度量回调。
- 新增 `QLITEMD_CORE_ONLY`：不查找 Qt，不编 qlitehtml/litehtml/
  spdlog/src/markdown/md_viewer，只构建 mdhtml、md4c、tree-sitter、
  QuickJS 与核心测试。
- CI：`.github/workflows/build.yml` 增加 `core-only` job
  （ubuntu-24.04，无 Qt，quickjs，全量 ctest + 格式检查）。
- 测试：新增 `test_mdhtml_pipeline`（8）与 `test_mdhtml_c_api`（3）。
  三种配置均 8/8 通过：本地 ratex+merman、本地 quickjs、
  本地 `build_core_only`（无 Qt）。

## S5 Qt 适配层迁移（完成）

- `src/markdown` 整体 `git mv` 到 `examples/md_viewer/qt_adapter`；
  `markdown_res.qrc` 的相对路径按新层级修正；目标名仍是
  `qlitemd_markdown`（S6 再处理安装/改名）。
- `MarkdownView` 的渲染主链路换到 `MdHtmlPipeline`：
  - `StartRender` 派发核心 job，等待线程只做 `job->Wait()` + SVG 光栅化
    （QImage/PNG 可在后台线程），结果经 queued invoke 回 GUI；
  - 资产在等待线程转成旧 `mj:f/<sha>`/`mj:m/<sha>` token，HTML 里的
    `qlitemd-asset:<id>` 被替换；公开 API、qlitehtml pixmap 回调、
    导出路径无需改动；
  - 持久 PNG 缓存继续使用原来的 formula/mermaid key 与二进制格式；
    命中时跳过光栅化并计入磁盘命中；
  - `QtMdFontMetrics` 注入核心 pipeline：CJK 族选择、QFontMetricsF、
    font fingerprint 与旧 mermaid 路径一致；
  - QtSvg 后端时在适配层对 Mermaid SVG 重新应用
    CleanSvg/InlineSvgCss/InlineSvgMarkers，resvg 路径保持原始 SVG；
  - 状态面板计数映射：渲染 = 任务数 - 磁盘命中，磁盘命中单独计数；
    renderer_status 的 loaded 标志按实际出现过的任务置位。
- 核心 `MdHeading` 增加 `id` 字段，适配层据此恢复 slug→id 锚点表。
- 验证：
  - `ctest -L MARKDOWN`：38/38 通过（含 markdown_view、导出、
    渲染开关、状态面板、md_viewer 模块测试）；
  - 本地截图（`temp/render-style-demo`）确认浅色/深色下公式、
    Mermaid、代码高亮、表格、大纲均正常；
  - quickjs 与 core-only 两种 mdhtml 构建仍 8/8。

已知取舍：
- 旧 render_service/cache 代码仍留在适配层（`MarkdownView` 已不再走
  它），测试仍有覆盖；S6 清理或迁移。
- 核心每次 ApplyDocument 都重新生成 SVG，磁盘缓存只省掉光栅化；
  内容级缓存留给后续核心版本。
- 状态面板的 per-lane 数字来自核心总计，暂不能区分三条 lane。
