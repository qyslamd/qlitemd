# resvg 与 merman 渲染后端接入方案

状态：Windows Qt5.15.2 x86/x64 与 Qt6.8.3 x64 已接入并通过测试；
Linux 未验证。
日期：2026-10-22。
分支：`rs-backend-svg`（由 `feat/resvg-renderer` 改名）。
配套实验：

- `temp/mermaid-pk/`：QuickJS + mermaid.js + QtSvg/resvg 并排对比；
- `temp/resvg-vs-qtsvg/`：resvg 静态库最小接入验证；
- `temp/merman-ffi-demo/`：merman-ffi + resvg 的 x64 demo 与结论；
- `temp/cmake-cargo-check/`：CMake 在构建期调用 `cargo build` 的验证。

## 决策记录

- **图族**：使用显式子集，不使用 `all-diagrams`。
- **数学**：Mermaid 内嵌公式不在本次范围，不编 `math` feature；
  公式继续走 MathJax。
- **QuickJS**：保留。MathJax 与 highlight.js 继续走 QuickJS，
  高亮不是瓶颈。
- **平台**：本次只验证 Qt 5.15.2 + MSVC2019 的 x86/x64；
  WSL/Linux、Qt6 暂不纳入。
- **分发**：merman 按 resvg 的方式收编，只收预编 DLL 与头文件；
  默认构建不调用 cargo。

## 一、目的

`MarkdownView` 的公式与 Mermaid 图最终都走 `QSvgRenderer`（QtSvg
模块）光栅化。生产环境长期暴露两类问题：

- Mermaid 图填充色错乱、文字对比度低、布局要素丢失；
- 为绕开 QtSvg 的限制，`mermaid_svg_cleanup.cpp` 维护了近千行的
  后处理（文本定标、CSS 内联、marker 展开），`formula_renderer`
  还有 `currentColor` 替换 hack，持续吃维护成本。

本方案包含两个可以独立落地、但配套收益最大的替换：

1. **光栅化后端**：`QSvgRenderer` -> resvg；
2. **Mermaid 生成后端**：QuickJS + mermaid.js -> merman-ffi。

merman 的 `resvg-safe` 输出是为 resvg 设计的；只换 merman、继续用
QtSvg，QtSvg 仍不认 CSS 类选择器，`mermaid_svg_cleanup` 的大部分
逻辑删不掉，收益只剩速度。因此建议把两项放在同一条迁移线上评估。

## 二、现状

当前链路：

```text
Markdown
  -> mermaid.js（QuickJS 字节码 + dom_shim + path_bbox）
  -> 原始 SVG
  -> mermaid_svg_cleanup（CSS 内联、marker 展开、文本定标）
  -> QSvgRenderer
  -> QImage / PNG data URI

Markdown 公式
  -> MathJax（QuickJS 字节码）
  -> SVG
  -> formula_renderer 的 currentColor 替换
  -> QSvgRenderer
```

关键事实：

- QuickJS 同时服务 Mermaid、MathJax 和代码高亮；merman 只替换
  Mermaid 生成，**不能因此删除 QuickJS**。公式与高亮仍然需要
  `md_mj`、`md_hl` 两份载荷。
- Mermaid 的 SVG 重度依赖 `<style>` 与类选择器；QtSvg 不支持，
  这是质量问题的根因，不是后处理能彻底解决的。
- `mermaid_svg_cleanup` 的文本定标是为了补 QtSvg 的 tspan 缺陷；
  换 resvg 后应按 diff 结果逐项删除，不能直接整文件删除。

## 三、resvg 评估结论

### 3.1 质量

用同一份 mermaid SVG 分别交给 QtSvg 与 resvg：

- 时序图：QtSvg 参与者框填充错误、文字低对比、字体回退成衬线；
  resvg 全部正确。
- 类图：QtSvg 类名、构造型、多重性丢失，布局塌陷；resvg 完整还原。
- 根因：QtSvg 不支持 `<style>` 块和 CSS 类选择器，mermaid 的填充、
  字色、字族大量由 CSS 指定。
- 公式：两侧质量相当；resvg 原生支持 `currentColor`，可删除
  `formula_renderer` 的替换 hack。
- x86 与 x64 结论一致；`3rdparty/resvg/bin/{x64,x86}/resvg.dll`
  均已验证可加载。

证据文件在 `docs/plan/resvg-vs-qtsvg/`。

### 3.2 性能

评估程序为未优化构建，绝对值只作相对参考。大图栅格化两侧同量级；
公式小图的 resvg 数字含每次重建 `resvg_options` 与系统字体扫描的
一次性开销，真实集成必须复用 options：

| 图表 | QtSvg | resvg | 位图 |
| --- | ---: | ---: | --- |
| sequence @2x | 166-190 ms | 211-237 ms | 2110x2046 |
| class @2x | 281-310 ms | 223-240 ms | 2849x1146 |
| formula @4x | 4-22 ms | 81-116 ms（含一次性开销） | 小图 |

结论：resvg 的收益在质量与维护成本，不在速度；正确复用 options 后
不引入速度回退。

## 四、merman-ffi 评估结论

验证环境：Windows 11、VS2019 x64、Qt 5.15.2 x64、merman 0.9.2、
resvg 0.48.1。merman 构建命令：

```bash
cargo build -p merman-ffi --release --locked \
  --no-default-features \
  --features svg,diagram-flowchart,diagram-sequence,diagram-class,\
diagram-state,diagram-er,diagram-gantt,diagram-pie,diagram-journey,\
diagram-git-graph,diagram-mindmap,diagram-timeline,\
diagram-quadrant-chart,diagram-requirement
```

### 4.1 四个必须同时满足的条件

1. **打开对应图族的 feature。** 只带 `svg` 时，时序图/类图返回
   `status=5 Unsupported diagram type`。生产使用固定显式子集，
   见第八节和第十节；不使用 `all-diagrams`。
2. **使用 `resvg-safe` 管线。** 默认 `parity` 输出把类图标签放在
   `<foreignObject>`，resvg 不认，文字全部消失。请求选项：
   `{"svg":{"diagram_id":"...","pipeline":"resvg-safe"}}`。
3. **提供宿主文本度量。** merman 的确定性度量与 Windows 字体不一致，
   类图标题框会窄 12%-22%，文字两端截断。需要用 `QFontMetricsF`
   实现 `text_measure` 回调；回调依赖 `QGuiApplication`，换成
   `QCoreApplication` 会访问违例。
4. **布局字体与 SVG 字体一致。** 类图布局默认按 Trebuchet MS 度量，
   SVG 根 CSS 却可能写 `"Recursive Variable",arial`；必须通过
   `site_config` 的 `fontFamily` 与 `themeVariables.fontFamily`
   把两边钉成同一族。`fontFamily` 不能放 options 根上，否则 merman
   以 `status=3` 拒绝。

### 4.2 质量

`theme:default` + `look:classic` + 统一字体后：

- 时序图：参与者、autonumber、loop/alt、激活条、note、跨线消息、
  中文文本全部正确；
- 类图：构造型、泛型、静态成员、关系标签、多重性 `2..8`、note
  全部正确，标题不再截断；
- 配色与当前 mermaid.js 11.16 默认主题一致。

### 4.3 性能

同一份 `.mmd`、同一台机器、Qt 5.15.2 x64，两边均 RelWithDebInfo；
只统计 Mermaid 源码到 SVG，不含 resvg 光栅化。merman 取预热后
3 轮中位数，mermaid.js 取 3 轮中位数：

| 阶段 | merman-ffi | QuickJS + mermaid.js | 差距 |
| --- | ---: | ---: | ---: |
| engine 初始化 | 7 ms | 138-299 ms | 约 20 倍 |
| sequence 首次生成 | 728 ms | 888 ms | 1.2 倍 |
| sequence 预热后 | 11-13 ms | 562-604 ms | 约 45-50 倍 |
| class 首次生成 | 48 ms | 8.9 s | 约 185 倍 |
| class 预热后 | 26-28 ms | 8.4-8.7 s | 约 300 倍 |

merman 的 728 ms 是首个图的一次性初始化；同进程后续每张图
11-28 ms。光栅化阶段两者同量级，class 的位图尺寸不同
（merman 2892x1180，mermaid 4806x1036），不能拿这组数字比较
resvg 本身的快慢。按“生成 SVG + 光栅化”粗算，sequence 约
180 ms 对 730 ms，class 约 166 ms 对 8.9 s。

## 五、成本与边界

- resvg、merman 都采用**预编二进制分发**：仓库收 DLL 与头文件，
  日常开发、CI 和终端用户不需要 Rust。
- 维护者升级时需要 Rust stable + MSVC 组件；resvg 构建见
  `docs/guide/resvg构建指南.md`。merman 的维护者构建流程单独写成
  `docs/guide/merman构建指南.md`，不进入默认 CMake 构建。
- merman 0.9.2 对齐 Mermaid 12.1.0，qlitemd 当前打包 11.16.0。
  语法、默认主题、布局都可能变化，必须有真实语料回归。
- merman 的 `resvg-safe` 只能回退纯文本标签；依赖富 HTML/CSS 的
  标签会丢样式。qlitemd 现在用 `htmlLabels:false` 规避了大部分，
  仍需逐图确认。
- merman 本身 Apache-2.0 OR MIT；不启用 `math`，也不启用
  `layout-elk`，因此不引入 OFL 字体数据和 EPL-2.0 的 ELK。
  分发前仍按 `THIRD_PARTY_NOTICES.md` 登记。
- 本方案不解决 MathJax 的 JS 依赖；QuickJS 继续保留，MathJax
  与 highlight.js 不变。若将来要彻底去 JS，需要另立公式渲染器
  方案。
- 本次只覆盖 Qt 5.15.2 + MSVC2019 的 x86/x64。WSL/Linux、Qt6
  的构建与字体回退不在本次验收范围。

## 六、目标架构

```text
Markdown
  -> MermaidBackend
       legacy: QuickJS + mermaid.js + dom_shim + path_bbox
       native: merman-ffi（resvg-safe）
  -> SVG
  -> SvgRasterizer
       legacy: QSvgRenderer
       native: resvg
  -> QImage / PNG data URI

Markdown 公式
  -> MathJax（QuickJS）
  -> SvgRasterizer
```

接口边界：

- `MermaidBackend::Render(code, theme, diagnostics)`：输入 Mermaid
  源码，输出 SVG；缓存 key 由后端版本、字体指纹、主题和源码组成。
- `SvgRasterizer::Render(svg, scale, diagnostics)`：输入 SVG，
  输出 `QImage`；负责 options 复用、字体族、错误映射。
- `MermaidToImgTag`、公式的 `SvgToImgTag` 保留为门面，内部走上述
  接口；调用方（`MarkdownRenderService`、缓存、诊断）不改协议。
- 两个后端都由开关选择，默认值在验证通过后再切换；保留一个版本
  周期的回退路径。

## 七、resvg 接入步骤

1. 新增 `src/markdown/resvg_renderer.{h,cpp}`，薄封装 C API：
   `resvg_options_create` -> `resvg_parse_tree_from_data` ->
   `resvg_get_image_size` -> `resvg_render` -> `QImage`。
   `resvg_options` 按线程/渲染上下文复用一个，避免公式小图每次
   重扫系统字体。
2. 在 `formula_renderer.cpp` 与 `mermaid_renderer.cpp` 的
   `SvgToImgTag` 中切换到 `SvgRasterizer`；先保留 QtSvg 实现，
   用 `QLITEMD_SVG_BACKEND=resvg|qtsvg` 选择。
3. CMake：
   - 新增 imported target `resvg::resvg`，按 `CMAKE_SIZEOF_VOID_P`
     选择 `3rdparty/resvg/bin/{x64,x86}/resvg.dll` 与对应 import lib；
   - 把 DLL 拷到可执行文件目录；
   - `install(FILES ... DESTINATION bin COMPONENT Runtime)`；
   - `NOTICE` 登记 Apache-2.0 OR MIT，安装 `LICENSE-APACHE`、
     `LICENSE-MIT`。
4. 删除 `currentColor` 替换：resvg 原生支持；暗色主题可用
   `resvg_options_set_stylesheet` 注入 `* { color: ... }`。
5. `mermaid_svg_cleanup` 分项处理：
   - CSS 内联、marker 展开：用 resvg 后端跑 `MD_MARKDOWN_DUMP`
     产物批量 diff，确认后再删；
   - 文本定标：先保留，逐图对比后再删；
   - 边缘清理（无尺寸 rect、`stroke-dasharray=0` 等）：逐项验证。
6. 验证：Qt5.15.2 MSVC2019 x86/x64；公式四组样张；离线字体；
   `ctest -L MARKDOWN`。

## 八、merman-ffi 接入步骤

### 8.1 产物与目录

默认走预编二进制，不要求 Rust：

```text
3rdparty/merman/
  include/merman.h
  include/merman_text_measurement_abi.h
  bin/x64/merman_ffi.dll
  bin/x64/merman_ffi.dll.lib
  bin/x86/merman_ffi.dll
  bin/x86/merman_ffi.dll.lib
  LICENSE-APACHE
  LICENSE-MIT
  THIRD_PARTY_NOTICES.md
  VERSION.md
```

`VERSION.md` 记录 merman 版本、Rust 版本、target、feature 组合、
SHA256；格式与 `3rdparty/resvg/VERSION.md` 一致。CMake 新增
imported target `merman::ffi`，按架构选择 DLL 与 import lib；
DLL 拷到可执行文件目录，随 `Runtime` 组件安装。

### 8.2 维护者构建

默认构建**不调用 cargo**，与 resvg 一致。维护者升级 merman 时按
`docs/guide/merman构建指南.md` 生成 x64/x86 DLL，再替换
`3rdparty/merman` 下的产物。本文第九节保留 CMake 调用 cargo 的
验证结果，但它只是能力验证，不作为产品构建路径。

构建命令（x64）：

```bash
cargo build -p merman-ffi --release --locked \
  --target x86_64-pc-windows-msvc \
  --no-default-features \
  --features svg,diagram-flowchart,diagram-sequence,diagram-class,\
diagram-state,diagram-er,diagram-gantt,diagram-pie,diagram-journey,\
diagram-git-graph,diagram-mindmap,diagram-timeline,\
diagram-quadrant-chart,diagram-requirement
```

x86 将 `--target` 换成 `i686-pc-windows-msvc`，并在
`vcvars32.bat` 环境中构建。Rust 版本由 merman 的
`rust-toolchain.toml` 钉在 1.95.0；x86 需要为这个 toolchain 额外
安装 `i686-pc-windows-msvc` target。

### 8.3 C++ 运行时封装

新增 `src/markdown/merman_renderer.{h,cpp}`，实现
`MermaidBackend`：

1. ABI 发现：`merman_get_native_api`，校验 ABI 3 与 layout digest；
2. 每个 worker 一个 engine（带 `text_measure` 的 engine 并发调用会
   返回 `BUSY`，与现有每 worker 一个 `JsRuntime` 的模型一致）。
   `MermanNativeEngineToken` 必须声明为 `alignas(8)`：x86 下普通
   `uint64_t` 局部变量不保证 8 字节对齐，merman 会返回
   `invalid-argument`，错误消息是
   `out_engine must be aligned to 8 bytes`；
3. engine options 用 `site_config` 固定字体、主题：
   ```json
   {
     "site_config": {
       "theme": "default",
       "look": "classic",
       "fontFamily": "<MermaidFontFamily()>",
       "themeVariables": { "fontFamily": "<MermaidFontFamily()>" }
     }
   }
   ```
   `fontFamily` 必须使用 `MermaidFontFamily()` 的实际 CJK 族，不能
   沿用 demo 的 Trebuchet；并确保 resvg 的 sans-serif/serif 回退族
   与它一致。
4. 请求选项：
   ```json
   {"svg":{"diagram_id":"...","pipeline":"resvg-safe"}}
   ```
5. `text_measure` 回调用 `QFontMetricsF`/`QTextLayout` 实现，覆盖
   demo 已验证的 6/7/8/11/15/16/18 操作；对 bbox extents、Y offset
   等操作先返回 `handled=0`，用语料确认是否需要补齐。
6. 错误映射：`status=3` options、`4` 无图、`5` 解析失败、`6` 渲染
   失败、`7` 不支持、`16` BUSY，分别映射到现有 `RenderDiagnostics`
   与用户可见错误；`result_free` 必须成对调用。
7. 缓存 key 加入 merman 版本、feature 集合、字体指纹、主题和
   `resvg-safe` 管线标识，避免升级后命中旧图。

### 8.4 删除与保留

- 删除 `mermaid.js`、`dom_shim.js`、`path_bbox.js` 的 qjsc 条目与
  `mermaid_renderer.cpp` 中的 QuickJS 路径；
- 保留 `md_mj`（MathJax）与 `md_hl`（highlight.js）；
- `mermaid_svg_cleanup` 先在 merman + resvg 路径上保留为“兜底”，
  按 diff 结果逐项删除，不直接删文件；
- `js_runtime`、线程模型、公式缓存继续复用。

### 8.5 图族与 feature

首版固定显式子集：

```text
svg
diagram-flowchart
diagram-sequence
diagram-class
diagram-state
diagram-er
diagram-gantt
diagram-pie
diagram-journey
diagram-git-graph
diagram-mindmap
diagram-timeline
diagram-quadrant-chart
diagram-requirement
```

- 不使用 `all-diagrams`；需要新增图族时改 feature 列表、重建 DLL、
  更新 `VERSION.md`，不需要改 C++ 代码。
- 不启用 `math`：Mermaid 内嵌公式不在本次范围，公式仍走 MathJax。
- 不启用 `layout-elk`/`layout-cytoscape`：不引入 EPL-2.0 的 ELK；
  现有语料是否使用 ELK 布局需要在阶段 0 确认。
- 首版未纳入的图族包括 architecture、block、C4、kanban、packet、
  radar、sankey、treemap、xychart、zenuml、swimlane 等；按需扩展。

## 九、CMake 调用 cargo build 的验证（能力验证，不进入默认构建）

已在 `temp/cmake-cargo-check/` 实测通过：

- CMake 3.19+，Ninja；
- VS2019 `vcvars64.bat` 环境；
- `find_program` 通过
  `HINTS "$ENV{USERPROFILE}/.cargo/bin"` 找到
  `C:/Users/slamdd/.cargo/bin/cargo.exe`，即使 PATH 里没有 cargo；
- custom target 执行：
  ```text
  cargo build -p merman-ffi --release --locked \
    --no-default-features \
    --features svg,math,diagram-sequence,diagram-class
  ```
- 增量构建输出 `Finished release profile ... in 1.89-3.13s`。
  该次验证用的是 `svg,math,diagram-sequence,diagram-class`，且没有
  带 `--target`，所以产物在 `target/release/`；生产子集与带
  `--target` 的产物路径见 8.5 和 8.2。

结论：**CMake 可以调用 cargo build**，但必须满足：

- Windows 下在 vcvars/MSVC 环境中执行，否则 Rust 链接阶段找不到
  `link.exe`；
- 构建期用 custom target，不要放 configure 期；
- 干净 target 目录会下载并编译全部 crate，耗时分钟级，CI 需要
  缓存或改用预编 DLL；
- x86 需要为 merman 钉定的 Rust 1.95.0 安装
  `i686-pc-windows-msvc` target（`rustup target add ... --toolchain
  1.95.0-x86_64-pc-windows-msvc`）；
- x86 DLL 已用 Qt5.15.2 x86 demo 实际加载并渲染；生产集成仍需复验；
- Linux 产物名与动态库后缀不同，本次不验证，后续另立。

结论是“可以让 CMake 调 cargo”，但本方案**不采用**这条路径：merman
按 resvg 收编，生产构建只消费预编 DLL。这样避免把 Rust 工具链变成
所有开发者的硬依赖，也避免 CI 在每次构建时下载 crate。

## 十、实现计划

范围锁定：Qt 5.15.2 + MSVC2019 的 x86/x64；不做 Linux/WSL、不做
Qt6、不做 Mermaid 内嵌公式、不启用 `all-diagrams`。merman 只收预编
DLL，默认构建不调用 cargo。

当前进度：A、B、C 已完成；D 中的 Windows 安装、许可证、构建指南与
x86/x64 测试已完成。旧 QuickJS/QSvgRenderer 后端仍保留为编译开关，
按计划延后一个版本删除。Linux/Qt6 未纳入本次验收。

验证结果（2026-10-22）：

- Qt5.15.2 MSVC2019 x86/x64：`ctest -L MARKDOWN` 20/20 通过；
- `qtsvg + quickjs` x86：20/20 通过；
- `resvg + quickjs` x86：20/20 通过；
- 安装 SDK 后构建 `examples/minimal` 并运行通过；
- `md_viewer` 用 13 个显式子集图族做 `MD_MARKDOWN_DUMP`，明暗主题均出图；
- Qt6.8.3 x64 在全新构建目录下编译并通过 `ctest -L MARKDOWN`；界面未单独
  手工验收。
- Linux 未验证；Linux/WSL 当前自动回退到 `qtsvg + quickjs`。

### 10.1 阶段 A：收编 merman

| 任务 | 内容 | 验收 |
| --- | --- | --- |
| A1 | 构建 x64/x86 预编 DLL；feature 用 8.5 的显式子集；记录 SHA256 | 两个架构都能被 dumpbin 识别；x86 DLL 能在 Qt5.15.2 x86 进程加载 |
| A2 | 建 `3rdparty/merman/`，放头文件、DLL、import lib、许可证、`THIRD_PARTY_NOTICES.md`、`VERSION.md` | 目录结构与 resvg 一致；`VERSION.md` 写清版本/target/feature |
| A3 | CMake 新增 `merman::ffi` imported target；按指针宽度选 bin 目录；POST_BUILD 拷 DLL | 不装 Rust 也能 configure/build |
| A4 | `install` 把 DLL 放到 `Runtime`，许可证放到 `Licenses`；`NOTICE` 登记 | 安装目录含 DLL 与许可证 |
| A5 | 写 `docs/guide/merman构建指南.md`：Rust 1.95.0、x64/x86 命令、`rustup target`、产物校验 | 维护者可按文档复现 DLL |

### 10.2 阶段 B：resvg 光栅化

| 任务 | 内容 | 验收 |
| --- | --- | --- |
| B1 | 新增 `SvgRasterizer` 接口与 `resvg_renderer.{h,cpp}`；封装 options 复用、parse、size、render、QRgb | 单元测试：简单 SVG 输出非空、尺寸正确 |
| B2 | `formula_renderer.cpp` 与 `mermaid_renderer.cpp` 的 `SvgToImgTag` 改用 `SvgRasterizer`；保留 QtSvg 实现 | 两个后端可切换；公式/图表都能出图 |
| B3 | CMake 接入 `resvg::resvg`；按架构选 DLL；拷到 bin；`Runtime` 安装；许可证 | x64/x86 构建、安装包均含 `resvg.dll` |
| B4 | `resvg_options` 按线程/上下文复用；字体族与 `MermaidFontFamily()` 对齐 | 公式小图无重复字体扫描；中文回退正确 |
| B5 | 用 `MD_MARKDOWN_DUMP` 批量 diff；删除 `currentColor` 替换；按 diff 删 cleanup 项 | 公式/图表像素对比无回退；清理项逐条有记录 |

### 10.3 阶段 C：merman 生成

| 任务 | 内容 | 验收 |
| --- | --- | --- |
| C1 | 抽 `MermaidBackend` 接口；QuickJS 实现改为 legacy adapter；`MermaidToImgTag` 保持门面 | 现有测试全过；缓存 key 协议不变 |
| C2 | 新增 `merman_renderer.{h,cpp}`：ABI 发现、每 worker 一个 engine、`resvg-safe`、`site_config` 字体/主题、错误映射 | 子集内图族都能出 SVG；错误码可诊断 |
| C3 | 实现 `text_measure`：覆盖 demo 已验证的 6/7/8/11/15/16/18；其余返回 `handled=0` | 类图标题不截断；中文文本度量正确 |
| C4 | CMake 编译开关 `QLITEMD_MERMAID_BACKEND=quickjs|merman`，默认 quickjs；merman 链 `merman::ffi` | 两种后端都能构建；切换不要求改业务代码 |
| C5 | 语料回归：子集图族、CJK、主题、缓存、并发、诊断；对比 SVG/PNG | 子集内无阻塞差异；性能记录在案 |
| C6 | 通过后切默认 merman，保留 QuickJS 一个版本周期 | 默认构建产物不再加载 mermaid.js |

### 10.4 阶段 D：清理与发布

| 任务 | 内容 | 验收 |
| --- | --- | --- |
| D1 | 删除 `mermaid.js`/`dom_shim.js`/`path_bbox.js` 的 qjsc 条目；保留 `md_mj`/`md_hl` | 构建不再生成三份字节码；公式与高亮正常 |
| D2 | 按 B5 记录删除 `mermaid_svg_cleanup` 的 CSS/marker/文本定标 | 删除项有 diff 证据；无回归 |
| D3 | 更新 README、NOTICE、安装包、许可证 | 文档与安装包一致 |
| D4 | 跑完整验证：Qt5.15.2 MSVC2019 x86/x64、`ctest -L MARKDOWN`、md_viewer 手工验收 | 全部通过后合入主线 |

### 10.5 依赖顺序与开关

```text
A（收编 DLL）
  -> B（resvg 光栅化）
  -> C（merman 生成）
  -> D（清理与发布）
```

- B 只依赖 A 的目录/CMake 结构，可与 C 的接口设计并行；
- C 依赖 B 的 `SvgRasterizer` 和 A 的 DLL；
- 默认值切换分两次：B 通过后切 resvg，C 通过后切 merman；
- 保留 `QLITEMD_SVG_BACKEND` 与 `QLITEMD_MERMAID_BACKEND` 两个
  编译开关，至少保留一个版本周期。

### 10.6 验证矩阵

| 项 | 范围 |
| --- | --- |
| 平台 | Windows Qt5.15.2 MSVC2019 x86/x64 |
| 图表 | flowchart、sequence、class、state、ER、gantt、pie、journey、gitGraph、mindmap、timeline、quadrantChart、requirement |
| 公式 | inline/display/分式/根号/中文 `\text`/暗色 |
| 字体 | 中文字体、缺字回退、离线环境 |
| 缓存 | 升级后 key 失效、主题切换、重复渲染 |
| 性能 | 单图预热、整篇多图、启动预热 |
| 诊断 | 解析失败、无图、BUSY、DLL 缺失、结果释放 |
| 测试 | `ctest -L MARKDOWN`，新增后端对比用例 |

### 10.7 粗估

- A：0.5 天；
- B：1-1.5 天；
- C：2-3 天；
- D：1-2 天。

合计约 5-7 个工作日，主要不确定性在 C 的语料覆盖与文本度量补齐，
不在 C ABI 接入本身。

## 十一、风险与待确认

- **版本差异**：merman 0.9.2 对齐 Mermaid 12.1，当前 11.16；
  语法/布局变化需要语料基线。首版接受显式子集内的行为差异，
  子集之外不承诺。
- **子集范围**：8.5 的 13 个图族是否覆盖你的实际文档？如果不够，
  在阶段 A 之前补进 feature 列表，避免 DLL 反复重编。
- **文本度量完整性**：demo 只验证了 6/7/8/11/15/16/18；其它图族
  可能触发 bbox extents/Y offset。C3 需要按语料补齐，这是 C 阶段
  的主要风险。
- **x86 实编**：x86 DLL 已构建并用 demo 渲染时序图/类图；注意
  engine token 的 8 字节对齐要求。生产集成仍需在 qlitemd 进程内
  复验。
- **二进制分发**：`3rdparty/merman` 只收预编 DLL，仓库体积增加
  约 30 MB（x64 16.7 MB + x86 13.2 MB，含头文件与许可证）；
  升级依赖维护者按指南重建。
- **回退**：保留一个版本的 QtSvg/QuickJS 双后端；回退开关在
  10.5，默认切换前必须验证。
- **许可证**：不启用 `math`/`layout-elk`，避免 OFL/EPL；仍要按
  `THIRD_PARTY_NOTICES.md` 登记 merman 自身与传递依赖。
- **外观差异**：merman 的 `theme:default` + `look:classic` 与当前
  mermaid 11.16 接近，但不是逐像素一致；默认切换前需确认可接受。

## 十二、结论

resvg 解决 QtSvg 对 mermaid SVG 的“不可用”问题；merman 把
Mermaid 生成从 QuickJS 的秒级降到十毫秒级，并去掉 mermaid.js、
dom_shim、path_bbox 这条 JS 链路。两者配套后，`mermaid_svg_cleanup`
的大部分兼容逻辑有望退出。

本次决策已收敛：

- 显式图族子集，不用 `all-diagrams`；
- 不做 Mermaid 内嵌公式，不编 `math`；
- QuickJS 保留，MathJax 与 highlight.js 不变；
- 只验证 Qt5.15.2 + MSVC2019 x86/x64，Linux/WSL 暂不做；
- merman 按 resvg 收编，只收预编 DLL，默认构建不调用 cargo。

按第十节的 A/B/C/D 顺序实施，先以开关接入、语料回归、双后端回退
的方式验证，再切默认。
