# Qt 无关的多线程 Markdown → HTML 管线设计

日期：2026-10-25
分支：`feature/qt-free-mdhtml`
状态：方案，未实施

## 1. 背景

当前 `src/markdown` 把 Markdown 解析、代码高亮、公式、Mermaid 和
qlitehtml 布局放在同一个 Qt 模块里：

- `ParseMarkdown()` / `CollectRenderTasks()` / `AssembleMarkdown()` 在
  GUI 线程执行（`MarkdownView::StartRender` 由 `QTimer` 投递）；大文档的
  解析与组装会卡 UI。
- 高亮、公式、Mermaid 的耗时块已经分到后台 worker，但 worker 是
  `QThread` + `QObject` 信号，类型是 `QString`/`QByteArray`/`QColor`，
  无法脱离 Qt 复用。
- 公式与 Mermaid 的最终产物是 `RenderedImage`（PNG），SVG 只是中间产物，
  随后被 resvg/QtSvg 光栅化。
- Mermaid 的文字排版依赖 `QFontMetricsF`/`QTextLayout`；merman 的 C
  回调也通过 Qt 做度量，且字体族选择要求 GUI 线程。
- qlitehtml 只吃 `QPixmap`，所以显示层必须做 SVG→位图；但这不应该
  反过来绑死 md→html 核心。

目标是把“Markdown 字节 → HTML + SVG 资源”做成一个独立的、零 Qt 的
C++17 库，解析、块渲染和组装都能在后台线程完成；Qt 只负责最后的
SVG 光栅化与 qlitehtml 显示。

## 2. 目标与非目标

### 目标

1. 新增零 Qt 依赖的核心库：公共头、实现、测试都不包含 Qt 头文件，
   不链接 Qt 库。
2. `md -> html` 全流程可异步、可多线程：解析在后台，高亮/公式/
   Mermaid 按块并行，组装按源码顺序确定性地合并。
3. 公式与 Mermaid 在核心里只产出 SVG；核心不产出 PNG、不调用
   resvg/QtSvg。
4. 文字度量可注入：默认给一个确定性的近似实现；宿主可注入真实
   字体度量（Qt 适配层、FreeType 等）。
5. 保持现有 `MarkdownView` 公共 API 不变；qt 适配层把 HTML + SVG
   接到 qlitehtml，行为、导出、缓存、状态面板尽量不变。

### 非目标

- 不在核心里做 HTML 布局、滚动、选区、剪贴板或窗口。
- 不在核心里决定主题 CSS；CSS 仍由宿主提供。
- 不更换 Markdown 方言（仍是 md4c 的 GFM 方言）。
- 不把 qlitehtml 换成浏览器核心。
- 不在第一阶段做跨进程/分布式渲染。

## 3. 总体架构

```text
调用方（Qt MarkdownView / 其他宿主）
        │  RenderAsync(markdown, options)
        ▼
┌─────────────────────────────────────────────────────────────┐
│ qlitemd_mdhtml（纯 C++17，零 Qt）                            │
│                                                             │
│  coordinator thread                                         │
│    parse (md4c) → collect tasks → assemble HTML             │
│         │                  │                                │
│         │                  ├─ highlight lane (tree-sitter)  │
│         │                  ├─ formula lane (ratex/quickjs)   │
│         │                  └─ mermaid lane (merman/quickjs) │
│         ▼                                                   │
│  MdHtmlResult { html, assets(svg), outline, stats }         │
└─────────────────────────────────────────────────────────────┘
        │  callback / future
        ▼
Qt 适配层
  SVG → QPixmap（resvg / QtSvg，按需）
  HTML + 图片资源 → QLiteHtmlWidget
  现有持久化 PNG 缓存 / 状态面板 / 导出
```

分层：

| 层 | 目标 | Qt | 职责 |
|---|---|---|---|
| 核心 | `qlitemd_mdhtml` | 否 | 解析、任务收集、线程调度、组装、SVG 资产 |
| 引擎 | `qlitemd_mdhtml_engines` | 否 | tree-sitter、RaTeX、merman、QuickJS 的 SVG 实现 |
| Qt 适配 | `qlitemd_markdown` | 是 | MarkdownView、litehtml、光栅化、缓存、主题资源 |
| 示例 | `md_viewer` | 是 | 窗口、面板、交互 |

引擎做成独立目标，是为了让只想要 Markdown→HTML 的宿主可以不带
RaTeX/merman/QuickJS；核心本身只依赖 md4c 和标准库。

### 备选方案

- **只把现有 ParseMarkdown/AssembleMarkdown 丢到 QThread**：改动最小，
  但公共/内部类型仍是 Qt，二进制仍依赖 Qt，无法给非 Qt 宿主复用。
- **用 pimpl 把 Qt 藏在内部**：调用方不写 Qt，但链接仍需 Qt 库，
  不满足“零 Qt 依赖”。
- **新核心 + Qt 适配层（本方案）**：核心完全 std/C，Qt 只做显示；
  代价是 port 工作量和 parity 风险，但边界最干净。

### 并行粒度

md4c 解析和最终 HTML 组装保持单线程：解析是有顺序的单遍状态机，
链接引用定义、HTML 块和占位符顺序都依赖上下文；把源码切块并行解析
会引入语义差异。真正的并行收益来自独立的块级工作：
代码高亮、公式、Mermaid。这也是当前异步服务已经验证过的并行点。
如果将来需要解析级并行，可以按顶层块分片并单独处理引用定义，但
不在第一阶段范围。

## 4. 公共 API 草案

遵循仓库“不使用 namespace”的约定，类型用 `Md` 前缀。

```cpp
struct MdColor {
  uint8_t r = 0;
  uint8_t g = 0;
  uint8_t b = 0;
  uint8_t a = 255;
};

enum class MdTheme { Light, Dark };
enum class MdAssetKind { Formula, Mermaid };
enum class MdSvgProfile {
  kPortable,  // 标准 SVG，resvg/浏览器可直接用
  kQtSvg,     // 追加 QtSvg 兼容后处理（可选）
};

struct MdFontSpec {
  std::string family;   // 解析后的族名；空 = 使用 provider 默认
  double size_px = 16.0;
  int weight = 400;
  bool italic = false;
};

struct MdTextMetrics {
  double width = 0.0;
  double ascent = 0.0;
  double descent = 0.0;
  int line_count = 1;
};

// 宿主注入；实现必须线程安全，可能同时被多个 worker 调用
class MdFontMetrics {
 public:
  virtual ~MdFontMetrics() = default;
  virtual std::string ResolveFamily(std::string_view css_list) const = 0;
  virtual MdTextMetrics Measure(std::string_view text,
                                const MdFontSpec& font) const = 0;
};

struct MdSvg {
  std::string svg;   // 完整 <svg>...</svg>，UTF-8
  int width = 0;     // intrinsic / viewBox 尺寸（CSS px）
  int height = 0;
};

struct MdAsset {
  std::string id;            // 稳定 hash，HTML 里用 qlitemd-asset:<id>
  MdAssetKind kind;
  std::string source;        // 公式 TeX 或 Mermaid 源码
  MdSvg svg;
  bool display = false;      // 独立公式
};

struct MdHeading {
  int level = 0;
  std::string text;
  std::string anchor;
};

struct MdCodeBlock {
  std::string id;
  std::string language;
  std::string code;
};

struct MdRenderOptions {
  MdTheme theme = MdTheme::Light;
  MdColor formula_color{};
  bool formula = true;
  bool mermaid = true;
  bool code_line_numbers = false;
  MdSvgProfile svg_profile = MdSvgProfile::kPortable;
  std::shared_ptr<const MdFontMetrics> font_metrics;  // 空 = 默认近似
  std::size_t highlight_workers = 2;
  std::size_t formula_workers = 2;
  std::size_t mermaid_workers = 4;
};

struct MdRenderStats {
  int source_chars = 0;
  int html_chars = 0;
  int headings = 0;
  int code_blocks = 0;
  int formulas = 0;
  int mermaid_blocks = 0;
  int asset_count = 0;
  int worker_tasks = 0;
  int worker_failures = 0;
  int64_t parse_ms = 0;
  int64_t collect_ms = 0;
  int64_t worker_ms = 0;
  int64_t assemble_ms = 0;
};

struct MdHtmlResult {
  bool ok = false;
  std::string error;          // parse/致命错误；空 = 成功
  std::string html;           // 正文 HTML（不含 html/style 外壳）
  std::vector<MdAsset> assets;
  std::vector<MdHeading> outline;
  std::vector<MdCodeBlock> code_blocks;
  MdRenderStats stats;
};

class MdRenderJob {
 public:
  virtual ~MdRenderJob() = default;
  virtual void Cancel() = 0;
  virtual bool IsFinished() const = 0;
  // 阻塞到完成；不要求从 GUI 线程调用
  virtual MdHtmlResult Wait() = 0;
};

using MdProgressCallback = std::function<void(int completed, int total,
                                              int in_flight)>;

class MdHtmlPipeline {
 public:
  explicit MdHtmlPipeline(const MdPipelineOptions& options = {});
  ~MdHtmlPipeline();

  // 解析、块渲染、组装全部在内部线程；回调在 coordinator 线程执行
  std::shared_ptr<MdRenderJob> RenderAsync(
      std::string markdown, MdRenderOptions options,
      MdProgressCallback progress = {});
  // 便捷同步入口：内部等待，但仍使用线程池
  MdHtmlResult Render(std::string markdown, MdRenderOptions options = {});

  void CancelAll();
  MdServiceStatus Status() const;  // 纯 C++ 结构，Qt 适配层负责映射
};
```

### 引擎注入

默认引擎由 `qlitemd_mdhtml_engines` 提供；宿主也可以替换：

```cpp
struct MdBackends {
  std::function<std::optional<std::string>(
      std::string_view code, std::string_view language)> highlight;
  std::function<std::optional<MdSvg>(
      std::string_view tex, bool display, MdColor color)> formula;
  std::function<std::optional<MdSvg>(
      std::string_view code, MdTheme theme)> mermaid;
};
```

空函数 = 关闭对应阶段，按原文/代码块回退。这样核心可以被单独
测试，也可以接别的引擎。

## 5. HTML 与资源契约

- `html` 是 UTF-8 的正文 HTML，不含 `<html>`/`<style>` 外壳；CSS
  仍由宿主套上。
- 公式与 Mermaid 输出：

  ```html
  <img src="qlitemd-asset:<id>" class="math-display" width="320" height="48">
  <img src="qlitemd-asset:<id>" class="mermaid" width="960" height="540">
  ```

  `width`/`height` 来自 SVG intrinsic size；宿主可用自己的资源
  handler 根据 `<id>` 取 `MdAsset.svg`。这样 HTML 不携带大段
  base64，也不要求 qlitehtml 认识 inline SVG。
- 导出场景：`MdHtmlResult::InlineAssets()`（计划中的辅助函数）把
  `<img>` 换成 `data:image/svg+xml;base64,...`，不依赖宿主资源表。
- 公式/Mermaid 失败：按现有行为回退为 `<code class="math-src">`
  或高亮后的代码块；`MdAsset` 不产生空项。
- 代码复制链接、标题 `id`/`data-md-slug`、大纲、代码块表沿用现有
  语义；HTML 结构尽量与当前 `AssembleMarkdown` 字节级一致，便于
  做 parity 测试。

## 6. 线程模型

### 6.1 线程

- 一个 coordinator 线程：解析（md4c）、收集任务、等待结果、组装。
  解析和组装不再占用 GUI 线程。
- 三个任务 lane（默认数量沿用当前经验值）：
  - highlight：tree-sitter，CPU 轻、任务多；
  - formula：RaTeX/QuickJS，单任务重；
  - mermaid：merman/QuickJS，单任务最重，默认 4 个。
- 每个 worker 持有自己的引擎状态：`TSParser`、QuickJS runtime、
  merman 句柄；这些对象不可跨线程共享。
- Windows 上 QuickJS 需要更大的原生栈（当前是 32MB）；核心用
  `_beginthreadex`/pthread attr 封装线程创建，不能直接用默认
  `std::thread` 栈。POSIX 用 `pthread_attr_setstacksize`。

### 6.2 任务与确定性

1. 解析阶段的占位符和块列表按源码顺序产生。
2. 任务按稳定 key 去重：
   - highlight：`(code, language)`
   - formula：`(trim(tex), display, color)`
   - mermaid：`(code, theme)`
3. 结果表以 `(kind, key)` 为索引；同一 key 只渲染一次。
4. 组装按源码顺序查表；worker 完成顺序不影响 HTML。
5. 资产 `id` 由 kind + key 的 SHA-256 前缀生成，保证跨进程稳定，
   也方便宿主的持久化缓存。

### 6.3 取消与进度

- 每个 job 一个 `std::atomic<bool> cancel`；排队任务出队时丢弃，
  正在跑的任务在块边界检查；QuickJS 通过 interrupt handler 中断。
- 结果带 generation id；过期结果不进入组装。
- 进度回调 `(completed, total, in_flight)` 由 coordinator 汇总；
  Qt 适配层用 `QMetaObject::invokeMethod` 投递到 GUI 线程。
- 取消后的 job 不触发成功回调；`Wait()` 返回空结果/取消标志。

### 6.4 线程数与背压

- `MdRenderOptions` 可覆盖三个 lane 的 worker 数；默认按当前
  2/2/4，实际以 `hardware_concurrency` 上限约束。
- 队列有界；超出时 `RenderAsync` 返回背压错误或阻塞提交线程
  （计划：每个 pipeline 一个有界队列，默认容量 256；Qt 适配层
  同一时间只保留一个活动 job，天然不会打满）。
- 进程级共享 pipeline 还是每个文档一个？建议核心提供可配置的
  共享池；Qt 适配层继续使用进程级单例，保证多标签/多视图复用。

## 7. 字体度量

### 7.1 接口

`MdFontMetrics` 是注入点，必须满足：

- `const` 且线程安全；
- `ResolveFamily(css_list)` 解析 CSS 字体族；
- `Measure(text, font)` 返回宽度/上高/下高；需要换行时返回
  `line_count`（或提供可选的 `MeasureWrapped`）。

### 7.2 默认实现

`MdDefaultFontMetrics`：

- 不读取系统字体，不依赖 Qt/FreeType；
- Latin 按平均字宽估算，CJK/全角按 1em，组合字符与 emoji 保守
  取 1em；
- family 用 `sans-serif`，解析 CSS 列表时取第一个已知通用族；
- 输出跨平台确定，适合无字体环境的服务端/测试。

默认实现只保证“能生成合法 SVG、文字不越界太多”，不保证与宿主
实际字体逐像素一致。Mermaid 的节点宽高会随度量变化，生产宿主
应注入真实度量。

### 7.3 Qt 适配层

`QtMdFontMetrics` 放在 `qlitemd_markdown`（Qt 层），实现：

- `ResolveFamily` 在 GUI 线程用 `QFontDatabase` 选 CJK 族并缓存；
- `Measure` 在线程内构造 `QFontMetricsF`/`QTextLayout`；
- 保持当前 `MermaidFontFamily` 的候选顺序与回退策略；
- 这样 worker 线程不需要 Qt 的 GUI 线程对象，也不会把 Qt 类型
  带进核心。

将来可以增加 FreeType/HarfBuzz provider，但不在本轮范围。

## 8. SVG 契约

### 8.1 核心只出 SVG

- 公式：RaTeX `ratex_render_svg` 或 QuickJS MathJax 的
  `<mjx-container>` → 提取 `<svg>`；注入公式前景色；解析
  width/height/viewBox。
- Mermaid：merman 的 `render` 输出 SVG，或 QuickJS mermaid 的
  `r.svg`；提取根元素；主题通过 mermaid init 指令处理。
- 核心不创建 `QImage`/`QPixmap`，不调用 resvg/QtSvg，不做 2x 采样。

### 8.2 SVG profile

- `kPortable`：只做根元素提取、XML 声明清理、公式 fill 注入；
  供 resvg/浏览器/其它宿主使用。
- `kQtSvg`：在 `kPortable` 基础上应用现有 QtSvg 兼容后处理
  （删无尺寸背景、`stroke-dasharray="0"`、tspan 定位烘焙、CSS
  内联、marker 展开、字体族强制）。这段代码目前是
  `mermaid_svg_cleanup.cpp`，可以：
  1. 保留在 Qt 适配层，只在 QtSvg 后端时调用；或
  2. 逐步移植成纯 C++，作为核心的可选 profile。
- 推荐先选 1，避免为了 QtSvg 的兼容性把核心的 SVG 后处理复杂化；
  如果宿主也需要 QtSvg，再做 2。

注意：当前顶层 CMake 规定 `merman` 必须搭配 resvg，是因为显示
管线要用 resvg 把 merman 的 SVG 光栅化。新核心只产出 SVG，不需要
链接 resvg；Qt 适配层再按自己的设置选 resvg 或 QtSvg。这个约束
只保留在 Qt/显示目标里，核心的 merman 后端可以独立使用。

### 8.3 尺寸与失败

- intrinsic size 用纯 C++ 解析 `width`/`height` 和 `viewBox`；
  无有效尺寸的 SVG 视为失败，回退原文/代码块。
- SVG 字符串设上限（例如 8 MiB/资产）防止异常输入耗尽内存；
  超限按失败处理并记录诊断。

## 9. 从现有代码迁移的映射

| 现有 | 处理 |
|---|---|
| `markdown_to_html.cpp` / `heading_ids` / `html_escape` | 改 `std::string`/`string_view`，算法与输出保持一致 |
| `code_highlighter` / `tree_sitter_highlighter` | 改 std 类型；tree-sitter query 从 qrc 改为构建期生成的 C 字符串/字节数组 |
| `formula_renderer` | 拆出 SVG 生产；去掉 `RenderedImage`/rasterizer；RaTeX/QuickJS 两条路径 |
| `mermaid_renderer` / `merman_renderer` | 拆出 SVG 生产；merman 度量回调接 `MdFontMetrics`；QuickJS bridge 改 std/JSON 手写 |
| `js_runtime` | 改 std 类型与 `std::chrono`，去掉 `QString`/`QByteArray`/`QElapsedTimer` |
| `mermaid_svg_cleanup` | 默认留在 Qt 适配层；需要时再按 profile 移植 |
| `svg_rasterizer` | 完全留在 Qt 适配层；核心不链接 resvg/QtSvg |
| `markdown_render_context` / `render_cache` / `render_service` | 核心换成 C++ 缓存/调度；Qt 适配层保留 PNG 持久缓存与状态映射 |
| `markdown_theme` | 核心用 `MdTheme`/`MdColor`；Qt 层做枚举/颜色适配 |
| `markdown_view` | 保留公共 API；内部改用核心 job，SVG 在 Qt 层光栅化 |

### 缓存

- 核心：内存结果表/去重（按 job 生命周期）；可选 LRU。
- Qt 适配层：现有 PNG 持久缓存继续保留，key 由资产 id + 输出缩放
  决定；升级后 pipeline 版本递增，旧缓存失效。
- 后续可加 SVG 持久缓存，供非 Qt 宿主直接复用；本轮不做。

## 10. CMake 与目录结构

建议新增：

```text
src/mdhtml/
  md_html_pipeline.h/.cpp
  md_parser.h/.cpp          // md4c + 公式抽取
  md_assembler.h/.cpp       // 代码块/公式/标题回填
  md_asset.h/.cpp           // SVG 提取、尺寸、颜色
  md_font_metrics.h/.cpp    // 接口 + 默认近似
  md_thread_pool.h/.cpp     // std::thread / 平台栈封装
  md_engine_state.h/.cpp
  md_backends.h
  engines/
    md_tree_sitter.cpp
    md_ratex.cpp / md_quickjs_formula.cpp
    md_merman.cpp / md_quickjs_mermaid.cpp
tests/mdhtml/               // GoogleTest，零 Qt
examples/minimal_core/       // 无 Qt 的控制台 consumer（可选）
```

CMake：

- `QLITEMD_BUILD_CORE`（默认 ON）：构建 `qlitemd_mdhtml`。
- `QLITEMD_CORE_ONLY`（默认 OFF）：不查找 Qt、不构建 qlitehtml /
  FluentUI / md_viewer；只构建核心、引擎、核心测试。
- `qlitemd_markdown` 通过 PRIVATE 链接 `qlitemd_mdhtml`；
  `MarkdownView` 的公共头仍不暴露核心类型（或逐步公开）。
- 安装：核心头文件进 `include/qlitemd/mdhtml/`，archive 进
  `lib`；`qlitemdConfig.cmake` 暴露 `qlitemd::mdhtml` 目标。
- 公共头不得包含 Qt；核心目标不得链接任何 Qt target。

## 11. Qt 适配层与迁移步骤

1. 核心先与 Qt 管线并存，`MarkdownView` 用开关选择旧/新。
2. 新适配器：
   - 调 `MdHtmlPipeline::RenderAsync`；回调 marshal 到 GUI 线程；
   - 收到 `MdHtmlResult` 后，用 `RasterizeSvg` 把每个 `MdAsset.svg`
     转成 1x/2x QPixmap（resvg 或 QtSvg，按现有配置）；
   - 按现有 `RegisterImage`/`mj:` token 机制注入 qlitehtml；
   - 复用 `code_blocks`、`heading_ids`、`outline`、状态面板。
3. 关闭旧路径后跑 30 个现有测试 + 新增核心测试；对比 HTML 输出。
4. 迁移持久缓存与导出：
   - HTML 导出走 `InlineAssets()`；
   - PDF 仍由 qlitehtml 按打印媒体重排；
   - PNG 缓存 key 加 pipeline 版本。
5. 最后移除旧 `ParseMarkdown`/`AssembleMarkdown`/Qt worker 实现，
   或保留为兼容包装。

## 12. 测试与 CI

### 核心测试（零 Qt）

- HTML parity：表格、任务列表、删除线、代码块、标题 slug、公式
  抽取、mermaid 围栏、转义、CJK/emoji；与当前输出做 golden 对比。
- SVG：根元素、intrinsic size、公式颜色、无 PNG、稳定 id、失败
  回退。
- 线程：假后端模拟不同延迟，验证输出顺序稳定、去重、取消、
  进度、背压；TSAN/ASAN 可选。
- 字体度量：假 provider 改变 Mermaid 尺寸；默认 provider 确定性。
- 零 Qt 检查：`QLITEMD_CORE_ONLY` 配置下不查找 Qt；CI 检查核心
  目标的 `ldd`/`dumpbin` 不含 Qt。

### CI

- 新增 `core-only` job：Ubuntu 24.04（不装 Qt）构建核心 + 测试；
  Windows x64 同样跑一遍。
- 现有 Qt 测试继续跑；新增 parity 用例。
- arm64 Focal job 可以选择只跑核心，缩短 QEMU 时间。
- 基准：公式/Mermaid/代码块混合文档，记录串行/并行耗时。

## 13. 性能验收

- 公式 100 个、Mermaid 20 个、代码块 200 个的文档，4 核上并行
  总耗时相对串行 ≤ 60%（即至少 1.7x）；具体数值以基准为准。
- 解析 + 组装不因 port 变慢超过 10%。
- GUI 线程不再执行 md4c 解析和最终组装；大文档首屏不因解析阻塞。
- 取消延迟 ≤ 100ms（不含正在跑的 QuickJS 单块；QuickJS 有中断）。
- 峰值内存不高于当前 PNG 管线；SVG 字符串按 8 MiB/资产上限。

## 14. 风险

1. **Port 面大**：当前 `markdown_to_html`、`mermaid_svg_cleanup`、
   renderers 合计大量 Qt 引用；必须分阶段 + parity 测试，避免一次性
   重写。
2. **字体度量差异**：默认近似会让 Mermaid 节点宽高与 Qt 不同；
   宿主注入真实度量才逐像素一致。文档与 API 必须明确。
3. **QtSvg 兼容**：核心默认输出标准 SVG；QtSvg 需要现有 cleanup。
   先留在 Qt 层，别把 QSvg 限制带进核心。
4. **线程安全**：QuickJS/merman/RaTeX 的实例不可共享；每 worker
   一份，pipeline 生命周期与 job 取消要严格。
5. **Windows 栈**：QuickJS 需要 32MB 原生栈；`std::thread` 默认栈
   不够，必须平台封装。
6. **缓存失效**：资产 id/PNG 缓存 key 变化会导致旧缓存 miss；用
   pipeline 版本号一次性失效。
7. **公共 API/安装**：核心头不得含 Qt；`qlitemd::mdhtml` 的安装与
   package config 需要新组件/文档。
8. **编译矩阵**：核心要在无 Qt 的 Linux/Windows 上构建；QuickJS
   字节码、tree-sitter query、字体度量都要可移植。

## 15. 阶段与工作量（粗估）

| 阶段 | 内容 | 估时 |
|---|---|---|
| 0 | 盘点 Qt 引用、建核心 target、无 Qt CI 骨架 | 0.5 天 |
| 1 | md4c 解析 + 组装 + heading/code/outline 的 std 化 | 2 天 |
| 2 | tree-sitter 高亮 std 化 + query 资源嵌入 | 1 天 |
| 3 | 公式/Mermaid SVG 生产 + 默认字体度量 + 注入接口 | 4 天 |
| 4 | std 线程池、取消、进度、确定性组装 | 2 天 |
| 5 | Qt 适配层（SVG→QPixmap、缓存、导出、状态） | 4 天 |
| 6 | parity/基准/文档/CI | 2 天 |
| 合计 | | 约 15.5 人日 |

## 16. 需要确认的决策

1. 核心是否作为公开 SDK（`qlitemd::mdhtml`）发布，还是先内部使用？
   建议先内部 target，parity 稳定后再安装公开。
2. 资产传输用 `qlitemd-asset:<id>` + assets 表（推荐），还是直接
   inline `<svg>`？inline 会让 HTML 很大，且 qlitehtml 不一定支持。
3. 高亮是否算核心职责？建议算，tree-sitter 已无 Qt，port 成本可控。
4. 默认字体度量用“确定性近似 + 注入”，还是内置真实字体度量表/
   FreeType？建议前者，后者作为可选 provider。
5. QtSvg 兼容层放 Qt 适配层（推荐，先不做核心 profile），还是核心
   提供 `kQtSvg` profile？
6. 线程数默认是否沿用 2/2/4？是否允许宿主按文档规模调整？
7. 是否保留旧 `MarkdownToHtml` 自由函数作为兼容包装？建议保留到
   下一个大版本。
8. 持久缓存继续由 Qt 层管，还是核心提供 SVG 磁盘缓存接口？

## 17. 结论

这个拆分能把“内容生成”和“Qt 显示”解耦：核心只依赖 C/C++ 库，
输出 HTML + SVG；Qt 层继续负责 qlitehtml、SVG 光栅化、缓存和
交互。多线程收益主要来自公式/Mermaid/高亮的块级并行，以及把
解析/组装移出 GUI 线程；零 Qt 的核心也为将来的非 Qt 宿主、服务端
预渲染和 arm64 等新平台复用留下空间。
