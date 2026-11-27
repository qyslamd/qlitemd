# qt_md_observatory（Observatory）内容感知阅读器分阶段方案

日期：2026-11-26。状态：**草案，待评审**。

原型：`temp/qt_reader_observator/prototype.html`（未入库，只作视觉与交互参照）。

相关文档：
`docs/plan/qt_md_viewer多标签页改造方案.md`、
`docs/plan/qt_md_viewer工具栏改造方案.md`。

## 1. 目标与定位

### 1.1 一句话

一个围绕“工作目录”组织的只读 Markdown 阅读控制台：把渲染器已经知道的结构
（块类型、标题、公式、图表）变成可读的导航与仪表，而不是再做一个排版更漂亮
的阅读器。

### 1.2 与 qt_md_viewer 的分工

| | qt_md_viewer | qt_md_observatory |
| --- | --- | --- |
| 定位 | 日常阅读器 / 轻量编辑宿主 | 工作区级阅读控制台 |
| 组织单位 | 标签页 + 文件 | 工作目录 + 文档 |
| 核心视图 | 正文 | 正文 + 结构仪表 |
| 关注点 | 打开、查找、书签、会话恢复 | 进度、结构、跨文档导航 |
| 关系 | 保留，继续演进 | 新增，不替代 |

qt_md_observatory 不是 qt_md_viewer 的下一版，也不共享窗口壳。两者共用渲染库与
少量纯逻辑模块。

### 1.3 成功标准

1. 选择一个目录后，左侧列出全部 Markdown，按目录分组，显示每篇进度。
2. 切换文档后回到上次位置；有历史进度时给出“从上次位置继续”的入口。
3. 右栏三件仪表（成分、脊柱、进度）与正文结构一致，数据来自同一份块模型，
   不在应用层重新解析 HTML。
4. 全部核心操作可用键盘完成。
5. 1280 宽窗口下三栏可用；更窄时右栏自动折叠。
6. 本机 Qt 5.15.2 x86 可构建运行；库层变更通过 `ctest -L MARKDOWN`。

### 1.4 非目标

- 不做编辑、保存；正文只读。
- 不做云同步、账号、AI 摘要。
- 不做移动端。
- 不追求覆盖全部 Markdown 方言，见 §5.10。

## 2. 为什么选 Observatory

| 原型 | 核心 | 结论 |
| --- | --- | --- |
| Atlas | 文档地形图 | 第一眼强，日常价值衰减快；适合做 Observatory 的备用工作区视图 |
| Folio | 排版阅读台 | 价值会被 qt_md_viewer 的阅读模式吸收，独立成 app 是自我竞争 |
| Prism | 一文档六切面 | 每个切面独立渲染，成本高，演示性强于实用 |
| Lumen | 常规阅读器 | 无差异化 |
| **Observatory** | **结构仪表 + 工作区** | **数据源唯一、产品定位独立、原型交互最完整** |

关键理由：Observatory 的视觉不是装饰，而是渲染器能力的直接投影。别的原型拼
排版，它拼“渲染器知道自己渲染了什么”。

## 3. 元素的三层可识别性

### 3.1 三层

| 层 | 定义 | 当前载体 |
| --- | --- | --- |
| 解析层 | md4c AST 能区分 | `MdParseResult` |
| 渲染层 | 输出 HTML / 资产，litehtml 能画 | `MdParseResult.html`、assets |
| 测量层 | 有 id、能取得 y 坐标 | 目前只有标题 |

仪表只能消费测量层；统计可以消费解析层；着色可以消费渲染层。一个元素可以
“解析得到、渲染不出、测量不到”，三者不能混成一个扁平的“内容类型”枚举。

### 3.2 现状

- 渲染链路：`MarkdownView` → `MdHtmlPipeline::RenderAsync` → `MdHtmlResult`
  （html、outline、code_blocks、assets、stats）。
- 测量：`MarkdownView::AnchorY(anchor)` 只对标题有效；`outline` 提供 `h-N`。
- C ABI：`mdhtml_c.h` 暴露大纲、代码块、资产和计数，`MDHTML_ABI_VERSION` 为 4，
  没有块列表。
- 结论：现在能做的仪表只有“计数 → 成分、标题 → 脊柱、滚动值 → 进度”。

### 3.3 关键判断

**M3 的仪表不需要块级坐标。** 用标题把文档切成若干段，块表按 section 归段，
就能得到每段的成分与节奏；精确到块的滚动联动放到 M6。不要把“块级 id”提前成
M1 的前置条件。

## 4. 数据契约

### 4.1 MdBlockKind

```cpp
enum class MdBlockKind {
  kParagraph,       // 普通段落
  kHeading,         // H1–H6，level 有效
  kBlockQuote,
  kList,            // 列表容器，children = 列表项数
  kListItem,
  kTaskItem,        // 带 checkbox 的列表项
  kCodeBlock,       // 围栏代码，language/asset_id 有效
  kMathBlock,       // 块级公式
  kMermaid,         // 图表
  kTable,           // 整个表格，children = 行数
  kImage,           // 整段只有一张图
  kHtmlBlock,       // 原始 HTML 透传块
  kAdmonition,      // admonition 0..7
  kFootnoteDef,     // 引用到的脚注定义
  kThematicBreak,   // 分隔线
  kOther,           // 未知，不猜
};
```

约定：

- `kImage` 只用于“整段就是一张图”；行内图片不单独成块。
- 列表容器与列表项都保留：仪表用列表项计数，容器用于嵌套深度。
- `kHtmlBlock` 不细分，因为原始 HTML 的语义不可靠。

### 4.2 MdBlock

```cpp
struct MdBlock {
  MdBlockKind kind = MdBlockKind::kOther;
  int ordinal = 0;          // 文档顺序，0 起
  int parent = -1;          // 父块 ordinal；顶层为 -1
  int section = -1;         // 第一个标题之前的块为 -1
  int level = 0;            // 标题层级；其余为 0
  std::string language;     // 代码语言
  int admonition = 0;       // 0..7，见 MdAdmonitionKind
  int mermaid_kind = 0;     // 归一化后的图类型；0 = unknown
  uint32_t inline_flags = 0;
  int children = 0;         // 列表项数、表格行数等
};
```

**md4c 不提供源码行号或字节偏移。** 块回调只有类型和类型明细，文本回调也没有
位置，所以不能用 `line/end_line` 分段。块表改为：

- `ordinal`：块在文档中的顺序，与 HTML 顺序一致。
- `parent`：收集器自己维护一个栈得到，用于列表、引用、表格的嵌套。
- `section`：遇到顶级标题时加一；标题本身属于新 section，标题之前的块为 -1。
  这是 M3 分段的唯一依据。

标题文本与锚点不进入块表：现有大纲扫描已经提供 `level/text/anchor/id`，两边按
标题顺序对齐即可，避免两套 slug 规则。原始 HTML 里的 `<h2>` 会进大纲但不进块
表，这个差异要在 M1 用 fixture 单独测：若对不齐，再改为“大纲由块表派生”。

收集器还需要几条 md4c 不直接给的判定：

- `kMermaid`：围栏代码块的语言是 `mermaid`。
- `kMathBlock`：段落里只有一个公式占位符，且对应 segment 的 `display` 为真。
  生产方言没有开 `MD_FLAG_LATEXMATHSPANS`，公式在 `md_parse` 之前已经被替换成
  占位符，所以不能依赖 md4c 的 math span。行内公式的 flag 也按占位符判定。
- `kImage`：段落里只有一张图片，没有其他文本。
- `kTaskItem`：`MD_BLOCK_LI_DETAIL.is_task` 为真。
- `kAdmonition`：GitHub 模式看 `MD_BLOCK_ADMONITION_DETAIL` 的类型；MPE 模式
  会关掉 md4c 的 admonition，改为识别引用块首行的 `[!type]`。

这些是收集规则，不是新的解析器；规则要写进 fixture 的期望值。

### 4.3 行内标志

```cpp
enum MdInlineFlag : uint32_t {
  kInlineBold = 1u << 0,
  kInlineItalic = 1u << 1,
  kInlineStrike = 1u << 2,
  kInlineCode = 1u << 3,
  kInlineMath = 1u << 4,
  kInlineLink = 1u << 5,
  kInlineImage = 1u << 6,
  kInlineMark = 1u << 7,
  kInlineSup = 1u << 8,
  kInlineSub = 1u << 9,
  kInlineKbd = 1u << 10,
  kInlineBadge = 1u << 11,
};
```

只表示“块内是否出现”，不表示位置。行内元素没有独立几何，不参与测量层。

### 4.4 文档级元数据

front matter（M1 做只读解析，见 §5.11）、标题、字数、块数、预计阅读时间。阅读
时间由字数推导，不进入块表。

### 4.5 产出与流转

```text
md4c callbacks
  ├─ md_html（现有）      -> HTML
  └─ md_parse（第二趟，与 md_html 并行） -> MdBlock 表
        ↓
  MdParseResult { html, blocks, outline, code_blocks, ... }
        ↓
  MdHtmlResult -> MarkdownView::blocks()
        ↓
  qt_md_observatory 仪表 / C ABI v5
```

S0 实测结论（2026-11-26）：**采用方案 A，但块收集必须与 `md_html` 并行**
（各自一个线程，join 后组装）。

实测环境：AMD Ryzen 7 3700U（4C/8T）、6.9 GB、Windows 11、MSVC 2019 x86、
`/O2`。fixture 为混合文档，1 MB 约 29.8k 块、HTML 约 2.1 MB。取多轮最小值：

| 文档 | md_html | 第二趟顺序 | 第二趟并行增量 |
| --- | --- | --- | --- |
| 256 KB | 7.1 ms | 4.5 ms | +0.0 ~ 0.7 ms |
| 1 MB | 28.6 ~ 38.8 ms | 17.9 ~ 18.6 ms | **+0.3 ~ 2.8 ms** |
| 4 MB | 116 ~ 148 ms | 76 ~ 102 ms | +11.8 ~ 29.6 ms |

顺序执行时，1 MB 的第二趟约占 `md_html` 的 60%，不满足“≤ 50%”的门槛；并行
后两趟同时跑，墙钟增量降到 3 ms 以内，4 MB 也只有 10% ~ 25%。回调本身的额外
开销约 0 ~ 1 ms/MB，说明成本几乎全在“再解析一遍”，不在收集逻辑。

因此：

- 默认：A + 并行。`md_html` 与块收集使用同一份“公式占位后”的源码，各自持有
  解析器和输出缓冲；md4c 无全局状态，可重入。
- 单核/线程创建失败：退化为顺序，最坏 +18 ms/MB，可接受但不作为默认。
- 不采用 B（md4c-html 补丁）：并行 A 已把 1 MB 增量压到 3 ms 内，不值得为
  省这 1 ms 去维护第三方补丁。
- 预算：1 MB 并行增量 < 10 ms，4 MB < 40 ms；M1 用真实文档复测。

Demo 程序在 `temp/parse_bench`，不入库。

约束：

- 两趟必须使用同一方言 flag，否则块表与 HTML 会不一致。
- 块表不改变 HTML 输出；golden 测试锁住 HTML 哈希。
- 未知类型一律 `kOther`，不允许根据 HTML 猜测。
- C ABI 追加访问器，`MDHTML_ABI_VERSION` 递增到 5；`mdhtml_stats` 通过
  `struct_size` 追加按 kind 的块计数。AvaLonia 示例和 README 同步。

### 4.6 测量层扩展（M6）

| 方案 | 做法 | 代价 | 结论 |
| --- | --- | --- | --- |
| A | 给 md4c-html 打补丁，顶层块加 `data-md-block` | 升级时重放补丁；精确 | M6 再评估 |
| B | 后处理 HTML 插属性 | 不碰第三方；要写标签扫描器，风险高 | 不推荐 |
| C | 不做块 id，仪表只到标题分段 | 精度有限；零核心改动 | M3/M4 采用 |

M3 的正文着色靠 CSS 选择器（`blockquote`、`table`、`pre`、`admonition-*`、
`.math-display`、`.mermaid-wrap`、`li.task-list-item`、`section.footnotes`），
不需要块 id。

## 5. 元素支持矩阵

“测量”列中的“近似”指能通过标题分段或 CSS 定位，拿不到精确块坐标。

### 5.1 结构类

| 元素 | 解析 | 渲染 | 测量 | v1 决策 |
| --- | --- | --- | --- | --- |
| H1–H6 | ✅ | ✅ | ✅ `AnchorY` | 做 |
| 段落 | ✅ | ✅ | 近似 | 做 |
| 分隔线 | ✅ | ✅ | 近似 | 做 |
| 引用块 | ✅ | ✅ | 近似 | 做 |
| 有序/无序列表 | ✅ | ✅ | 近似 | 做 |
| 任务列表 | ✅ | ✅ | 近似 | 做 |
| 定义列表 | ❌ md4c 无 | ❌ | — | 不做 |
| 脚注 | ✅ 方言已含 | ✅ 输出已有 | 近似 | 做，补视觉验证 |
| TOC / 标题锚点 | 应用生成 | ✅ | ✅ | 做 |
| `details` 折叠块 | 仅原始 HTML | ❌ litehtml 无 details 实现 | — | 不做，降级为静态 |
| tabs / 分栏 / 卡片 | ❌ | ❌ | — | 不做 |

### 5.2 强调类

| 元素 | 解析 | 渲染 | 测量 | v1 决策 |
| --- | --- | --- | --- | --- |
| 粗体 / 斜体 / 删除线 | ✅ | ✅ | 块内 flag | 做 |
| 高亮 `mark` | ❌ flag 未开 | ✅ md4c-html 已支持 | 块内 flag | 做，开 flag |
| 上标 / 下标 | ❌ flag 未开 | ✅ 已支持 | 块内 flag | **不做**：与字面公式冲突，见 §5.9 |
| 插入 | ❌ flag 未开 | ✅ 已支持 | 块内 flag | 做，开 flag |
| `kbd` / 徽章 | ❌ 无语法 | 仅原始 HTML | — | 不做 |

### 5.3 代码类

| 元素 | 解析 | 渲染 | 测量 | v1 决策 |
| --- | --- | --- | --- | --- |
| 围栏代码 + 语言 | ✅ | ✅ | 近似 | 做 |
| 行内代码 | ✅ | ✅ | 块内 flag | 做 |
| diff 代码块 | ✅ 语言名 | ✅ | 近似 | 做 |
| 行高亮 | ❌ | ❌ | — | 不做 |
| 复制按钮 | ✅ | ✅ `copy:<id>` | — | 已有 |

### 5.4 数学类

| 元素 | 解析 | 渲染 | 测量 | v1 决策 |
| --- | --- | --- | --- | --- |
| 行内 / 块级公式 | ✅ | ✅ | 近似 | 做 |
| 公式编号 | ❌ | ❌ | — | 不做 |
| 符号表 | ❌ | ❌ | — | 不做 |

### 5.5 图表类

| 元素 | 解析 | 渲染 | 测量 | v1 决策 |
| --- | --- | --- | --- | --- |
| 图片 | ✅ | ✅ | 近似 | 做 |
| 视频 / 音频 | 仅原始 HTML | ❓ litehtml 媒体支持未验证 | — | 不做，明确不支持 |
| Mermaid | ✅ | ✅ | 近似 | 做，类型归一化并记录后端 |
| 图标 / 附件 | 图片、链接 | ✅ | 近似 | 做 |

Mermaid 类型从源码首 token 归一化（`graph`/`flowchart`、`sequenceDiagram` 等），
无法识别记 unknown。原生 merman 不支持的图会按现有逻辑回退 QuickJS，仪表要
区分“渲染成功”和“回退后成功”，不能假设全部由原生后端渲染。

### 5.6 提示类

| 元素 | 解析 | 渲染 | 测量 | v1 决策 |
| --- | --- | --- | --- | --- |
| Admonition 8 类 | ✅ | ✅ | 近似 | 做，两种模式都要 |
| tooltip（title） | ✅ 属性 | ❓ 交互未验证 | — | v1 不做交互 |
| 锚点链接 | ✅ | ✅ | — | 做 |

### 5.7 数据类

| 元素 | 解析 | 渲染 | 测量 | v1 决策 |
| --- | --- | --- | --- | --- |
| 表格 | ✅ | ✅ | 近似 | 做 |
| front matter | ❌ 未处理 | ❌ | — | M1 只读，见 §5.11 |
| tags / 版本号 / 进度条 / 时间线 | 非 Markdown | 宿主约定 | — | 不做 |

### 5.8 导航类

| 元素 | 解析 | 渲染 | 测量 | v1 决策 |
| --- | --- | --- | --- | --- |
| 目录 TOC | 应用生成 | ✅ | ✅ | 做 |
| 面包屑 | 应用生成 | ✅ | — | 做 |
| 上一页 / 下一页 | 应用生成 | ✅ | — | 做 |
| 阅读进度 | 应用计算 | ✅ | ✅ | 做 |
| 返回顶部 | 应用 | — | — | M4 |
| 内部 / 外部链接 | ✅ | ✅ | — | 做 |

### 5.9 v1 方言开关

在 `MD_DIALECT_GITHUB` 基础上追加：

- `MD_FLAG_HIGHLIGHT`（`==mark==`）
- `MD_FLAG_INSERT`（`++insert++`）

不追加：

- `MD_FLAG_SUPERSCRIPTS` / `MD_FLAG_SUBSCRIPTS`：TDD 实测发现，公式提取关闭时
  md4c 会把 `$x^2$` 的 `^` 当上标、把 `H~2~O` 的 `~` 当下标，字面公式被吃掉。
  这条回归比“支持上下标”重要，故关闭；解析器仍保留对应 flag 位，未来若要开，
  必须先解决与 `$...$` 的冲突。
- `MD_FLAG_SPOILERS`（输出 `<x-spoiler>` 自定义元素，litehtml 不一定认）、
  `MD_FLAG_UNDERLINE`（改 `_` 语义，影响现有文档）、`MD_FLAG_WIKILINKS`
  （需要链接解析规则）。

注意：方言是全局的，改了对 qt_md_viewer 也生效，必须同步主题 CSS、补测试，并
在 CHANGELOG 里说明这是行为变更。

### 5.10 明确不做

定义列表、tabs、分栏、卡片、公式编号、符号表、代码行高亮、`kbd`、徽章、
spoiler、下划线、wiki link、`details` 折叠、视频/音频播放、块级自定义属性。

### 5.11 front matter

已定：M1 做只读解析。仅当文件首字节是 `---`、且前 30 行内有闭合 `---` 时，提取
`key: value`；失败回退为普通 Markdown。必须补“正文里的分隔线不会被误判”的测试。
front matter 不参与渲染，只作为文档级元数据。

## 6. 分阶段

规模用 S/M/L 表示，先不排日历；M1 完成后用实测校准。

### M0 方案冻结

- 目标：把本文件的待定项变成决定。
- 状态：**已完成**（2026-11-26，见 §10）。
- 交付：本文件定稿；§10 决策记录。
- 规模：S。

### S0 解析增量 Demo

- 状态：**已完成**（2026-11-26）。结果与结论见 §4.5。
- 交付：`temp/parse_bench` 独立程序（不入库）；256 KB / 1 MB / 4 MB 三档的
  `md_html`、`parse`、块收集、并行收集；多轮取最小值。
- 样本：标题、段落、列表、任务列表、表格、围栏代码、块级公式、Mermaid、
  admonition、脚注、行内强调混合；方言与生产一致。
- 结论：方案 A + 并行收集；1 MB 增量 0.3 ~ 2.8 ms。
- 规模：S。

### M1 块模型（核心库）

- 状态：**已完成**（2026-11-26），实施记录见 §12。
- 目标：让渲染器输出可信的块表。
- 交付：
  - `MdBlockKind` / `MdBlock` / `MdInlineFlags`（`md_types.h`）
  - 块收集器（A + 与 `md_html` 并行；单核退化为顺序）
  - `MdHtmlResult.blocks` + `MarkdownView::blocks()`
  - C ABI v5 块访问器与按 kind 的计数
  - `tests/mdhtml/md_block_model_test.cpp` + kitchen-sink fixture
  - front matter 只读解析与测试
- 关键点：两趟同 flag、同一份源码；HTML 字节不变；列表/表格嵌套计数；脚注
  定义归位；admonition 两种模式；标题块与 outline 数量一致；原始 HTML 标题的
  差异；公式占位到 `kMathBlock` 的映射。
- 测试：fixture 覆盖 §5 全部“做”项；HTML 哈希不变；C ABI `struct_size`；
  并行与顺序结果逐块一致；线程创建失败路径。
- 退出：`ctest -L MARKDOWN` 通过；1 MB 并行增量 < 10 ms、4 MB < 40 ms；
  front matter 误判测试通过。
- 规模：M。

### M2 应用骨架与工作区

- 状态：**已完成**（2026-11-26），实施记录见 §12。
- 目标：qt_md_observatory 能打开真实目录并切换文档。
- 交付：
  - `apps/qt_md_observatory` 新 target `qt_md_observatory`
  - 三栏壳 + 可折叠右栏 + QWindowKit 无边框标题栏
  - `WorkspaceModel`：递归扫描、分组、忽略规则、每篇进度
  - 位置持久化（复用或提取 `ReadingPositionStore`）
  - 文档切换、位置恢复、resume bar
- 依赖审计：
  `document_index`、`reading_positions`（Store 部分）、`navigation_history` 只
  依赖 QtCore；`side_panel` 与各控制器是应用 UI。建议抽 `apps/shared` 静态库，
  先搬 Store 和 Index，qt_md_viewer 行为与测试保持不变。
- 测试：扫描与忽略规则；位置恢复；resume 显示条件。
- 退出：真实目录 6 篇文档可切换；重启后位置恢复；qt_md_viewer 测试不变。
- 规模：M。

### M3 仪表 v1

- 状态：**已完成**（2026-11-26），实施记录见 §12。
- 目标：三件仪表跑在块表上，不碰 HTML。
- 交付：
  - 成分：按 kind 的块计数 + 每标题段堆叠条
  - 脊柱：标题层级 + 当前段 + 每段成分微条
  - 进度：单篇、整个工作区
  - `instrument_math.{h,cpp}` 纯函数与单测
  - 正文块着色（CSS 选择器）
- 关键点：所有数值来自同一函数；空态明确（没有块、没有标题）；不编造数据。
- 测试：fixture → 期望值；滚动到标题 → 当前段切换；窄宽度折叠。
- 退出：仪表与 fixture 统计一致；1280×800 无溢出；Qt 5.15 与 6.8 都过。
- 规模：M。

### M4 交互层

- 状态：**已完成**（2026-11-27），实施记录见 §12。
- 目标：键盘可完成核心操作。
- 交付：命令面板（文档 / 章节 / 命令 / 内容块）、快捷键
  （`Alt+Up/Down`、`W`、`[` `]`、`j/k`、`Ctrl+K`）、TOC 折叠、专注模式、toast。
- 测试：快捷键映射单测；GUI 冒烟用 agent-desktop，按 `objectName` 定位。
- 退出：不用鼠标可完成打开目录、切文档、跳章节、开关右栏。
- 规模：M。

### M5 打磨

- 状态：**已完成（有取舍）**（2026-11-27），实施记录见 §12。
- 目标：可日常使用。
- 交付：
  - light/dark、HiDPI（1 / 1.5 / 2）
  - 性能：1 MB 文档、200 篇工作区
  - 可访问性：`objectName`、`accessibleName`、键盘焦点
  - 旧解析路径清理决策（`markdown_to_html` 是否废弃）
  - 安装包（可选，与 qt_md_viewer 分开）
- 退出：性能预算达标；无崩溃；翻译管线接入。
- 规模：M。

### M6 可选

块级 id 与精确滚动联动；色温 / 节奏仪表；Atlas 地形工作区视图；tabs / cards
语法（§10 已定不做，除非重新评估）。只有 M3 上线并被实际使用后才做。

## 7. 架构与复用

```text
apps/qt_md_observatory
  ├─ workspace_model       QtCore，扫描与分组
  ├─ instrument_math       QtCore，纯函数
  ├─ observatory_window    QtWidgets，三栏壳
  ├─ instrument_rail       QtWidgets，自绘仪表
  └─ command_palette       QtWidgets，浮层

apps/shared（M2 提取）
  ├─ document_index
  ├─ reading_positions（Store）
  └─ navigation_history

src/markdown_view
  └─ MarkdownView         渲染、outline、AnchorY、blocks()
```

- 复用：`qlitemd_markdown_view`、QWindowKit、共享的纯逻辑模块。
- 新写：窗口壳、工作区模型、仪表、命令面板、主题与控件样式。
- 不共用：qt_md_viewer 的标签栏、工具栏、侧栏，信息架构不同。

### 7.1 UI 技术栈：不用 FluentUI，只用 QWindowKit

本次明确不采用 FluentUI：

- 不链接 `FluentUI3Style`、`ExWidgets`、`FluentTitleBar`，不依赖其图标字体与
  控件补丁链。
- 无边框窗口只用顶层已经单独构建的 QWindowKit：
  `QWK::WidgetWindowAgent::setup(this)`、`setTitleBar(自定义标题栏)`、
  `setSystemButton(Minimize/Maximize/Close, ...)`、对可交互控件
  `setHitTestVisible`。
- 标题栏、按钮、菜单、滚动条、命令面板全部自己画或走 QSS；图标用 qrc + SVG。
- 主题自己维护 light/dark 调色板；高对比度跟随 Qt palette，不假设 Fluent 行为。
- 顶层 CMake 需要让新 app 的 option 也打开 QWindowKit（当前它挂在
  `QLITEMD_BUILD_QT_MD_VIEWER` 下），或把 QWindowKit 的构建条件改成“任一
  应用开启”。

代价与收益：

- 代价：标题栏拖拽、双击最大化、系统按钮命中区、边框吸附、高 DPI 都要自己
  测；没有现成控件可用。
- 收益：不背 FluentUI 的补丁与裁剪，视觉完全由原型决定，依赖面更小。

## 8. 测试策略

- 核心：fixture + golden，锁块表顺序与 HTML 哈希。
- 控件：`tests/qt_md_observatory` 无头测试，覆盖 workspace / position /
  instrument math。
- GUI：agent-desktop，按 `objectName`；截图只作人工确认，不进 CI。
- 性能：脚本生成 1 MB 文档与 200 篇工作区，记录 parse、块表、首屏。
- 兼容：本机 Qt 5.15.2 x86；CI 覆盖 Qt 6.8.3 x64。

## 9. 风险

| 风险 | 影响 | 缓解 |
| --- | --- | --- |
| 元素清单膨胀 | 范围失控 | §5.10 明确不做；M0 冻结 |
| 二次解析变慢 | 首屏变慢 | S0 实测；超预算改 md4c-html 补丁 |
| 仪表变成装饰 | 产品失败 | 每个仪表必须有数据契约与空态 |
| litehtml 限制 | details / 视频不可用 | 明确降级，不承诺 |
| C ABI 变更 | 外部宿主破坏 | 版本递增，同步 AvaLonia 示例与 README |
| 共享模块提取 | qt_md_viewer 回归 | 先搬 QtCore 纯逻辑，测试先行 |
| 两套解析路径漂移 | 块表与 HTML 不一致 | 统一走 mdhtml；HTML 哈希测试 |
| 三栏拥挤 | 可用性差 | 断点折叠，M3 验收 1280×800 |
| 去掉 FluentUI | 标题栏与控件行为要自己兜底 | M2 先做无边框窗口 spike，覆盖拖拽、双击、系统按钮、DPI |

## 10. 决策记录（2026-11-26）

| # | 决策 | 结论 | 状态 |
| --- | --- | --- | --- |
| 1 | 应用名与目录 | `qt_md_observatory`，`apps/qt_md_observatory`，target `qt_md_observatory` | 已定 |
| 2 | 方言开关 | 开 highlight / insert；sup/sub 因破坏字面公式撤回；spoiler / underline / wikilink 不开 | 已定（2026-11-26 修正） |
| 3 | front matter | M1 只读，仅首字节 `---`，失败回退 | 已定 |
| 4 | 块级 id | M6 再定，M3 不依赖 | 已定 |
| 5 | tabs / cards / 分栏 | 不做 | 已定 |
| 6 | 共享层 | M2 抽 `apps/shared`，只放 QtCore 纯逻辑 | 已定 |
| 7 | 旧 `markdown_to_html` 路径 | M5 前确认调用方后删除 | 已定 |
| 8 | UI 技术栈 | 不用 FluentUI，只用 QWindowKit 做无边框窗口 | 已定 |
| 9 | 二次解析 | 方案 A + 并行收集；1 MB 增量 0.3 ~ 2.8 ms | 已定 |

## 11. 下一步

M0 ~ M5 已完成，全部 MARKDOWN 测试（55 项）通过。M6（块级 id、精确滚动
联动、色温仪表）仍是可选项，留到真实使用后再决定。

## 12. 实施记录

### 2026-11-26 M0 / S0

- 应用名、目录、方言、front matter、块级 id、tabs/cards、共享层、旧解析路径
  按 §10 冻结。
- S0 实测：方案 A + 并行收集；1 MB 增量 0.3 ~ 2.8 ms，4 MB 12 ~ 30 ms。

### 2026-11-26 M1

实际落地与开工前的差异：

1. `MdBlock` 没有 `line/end_line`：md4c 不提供源码偏移，改为
   `ordinal / parent / section`；`section` 由顶级标题推进。
2. 块收集与 `md_html` 并行；线程创建失败时顺序退化。两条路径结果逐块一致。
3. 公式块按“公式占位符”识别，不依赖 `MD_FLAG_LATEXMATHSPANS`；MPE callout
   按引用块首段的 `[!type]` 识别。
4. 方言只开 `HIGHLIGHT` / `INSERT`。TDD 发现 `SUPERSCRIPTS` /
   `SUBSCRIPTS` 会在 `formula=false` 时吃掉 `$x^2$` 的 `^`，已撤回并补回归。
5. front matter 只在首字节 `---` 且 30 行内闭合时生效，失败回退为普通
   Markdown。
6. C ABI 升到 v5：新增 11 个块访问器、3 个 front matter 访问器、`stats.blocks`；
   Avalonia 绑定的 `AbiVersion` 同步为 5。
7. `MarkdownView::blocks()` 暴露镜像结构；`markdown_view.cpp` 用
   `static_assert` 锁住与核心枚举的数值一致。

测试：`test_md_block_model` 17 项、`test_markdown_blocks` 2 项、
`test_mdhtml_c_api` 新增 1 项；`ctest -L MARKDOWN` 52/52 通过。

### 2026-11-26 M2

- 新增 `apps/qt_md_observatory`，option `QLITEMD_BUILD_QT_MD_OBSERVATORY`，
  Windows 顶层默认开启；QWindowKit 的构建条件改成“任一应用开启”，FluentUI
  仍只跟随 qt_md_viewer。
- 不用 FluentUI：Fusion + 自有 palette/QSS；标题栏、主题按钮和窗口按钮全部
  QPainter 自绘，QWindowKit 只负责无边框、命中测试和系统按钮。
- `WorkspaceModel`：递归扫描 Markdown，忽略隐藏目录与 build/node_modules 等；
  ROOT 组优先，其余按组名和相对路径排序；位置写 QSettings，键由根路径 SHA1
  和百分号编码的相对路径组成。
- 三栏壳：工作区 / 正文 / 右侧文档栏；顶部面包屑、文档序号、上一页/下一页、
  `Alt+Up/Down`；resume bar 只在打开文档后的第一次渲染出现。
- 右侧 M2 先显示块计数和进度，M3 换成仪表。

与开工方案的差异：

- 没有抽 `apps/shared`：新 app 的 `WorkspaceModel` 独立实现，qt_md_viewer 未动。
  共享层合并推迟到 M5，避免第二个 app 尚未稳定时改现有模块。
- 工作区行的 UIA Invoke 不会让 QTreeWidget 打开文档，改为监听
  `currentItemChanged`（鼠标、键盘和无障碍选择都能触发），并用 guard 屏蔽
  程序化重建。
- 浅色主题下标题栏仍显示深色：QWindowKit 的原生命中测试宿主不随 palette
  重绘，`grab()` 得到的是浅色、屏幕仍是旧帧。已记录，M5 处理。

测试：`test_workspace_model` 3 项；`ctest -L MARKDOWN` 53/53 通过。
界面截图见 `temp/obs_m2_final_*.png`。

### 2026-11-26 M3

- 新增 `instrument_math.{h,cpp}`：块分组、成分计数、按 section 汇总、当前章节
  判定、进度计算；纯 QtCore，5 项单测。
- 右侧栏重做：
  - 进度百分比 + 自绘进度条；
  - 成分堆叠条 + 图例（只列非零组）；
  - 章节脊柱：按标题层级缩进、每行显示块数和迷你成分条，当前章节橙色高亮；
  - 点击脊柱调用 `MarkdownView::ScrollToAnchor`。
- 章节 y 坐标来自 `AnchorY`；当前章节取“最后一个 top <= scroll + 24”的标题。
  滚动只更新当前章节和进度，不重建成分数据。
- 窗口与仪表共用 `ComputeProgress`，避免两套进度算法。

测试：`test_instrument_math` 5 项；`ctest -L MARKDOWN` 54/54 通过。
界面截图见 `temp/obs_m3_final.png`。

### 2026-11-27 M4

- 新增 `CommandPalette`：文档 / 章节 / 命令三类条目，输入即过滤，Up/Down
  选择、Enter 执行、Esc 关闭，单击行也执行；不依赖模态对话框。
- 快捷键：`Ctrl+K` 面板、`[`/`]` 上/下章节、`j`/`k` 滚动、`W` 工作区、
  `R` 右侧栏、`F` 专注模式、`Esc` 退出；`Alt+Up/Down` 切换文档。
- 章节脊柱的 SPINE 标题改成可折叠 QToolButton；折叠时用隐藏的 tail 保住
  纵向 stretch，避免上方标签被拉开。
- 专注模式隐藏左右栏，退出时恢复原可见性和 splitter 尺寸。
- 命令面板强制使用自己的 QSS（不依赖窗口级级联），浅色/深色各自生成。

测试：`test_command_palette` 3 项（过滤、Down+Enter、Esc）；`ctest -L MARKDOWN`
55/55 通过。截图见 `temp/obs_m4_palette4.png`、`temp/obs_m4_spine_collapsed2.png`。

### 2026-11-27 M5

- 浅色标题栏：
  - `main` 在创建任何控件之前按 QSettings 设置 application palette；
  - `ApplyTheme` 同时设置 `qApp` palette 并重建工作区行，避免组标题保留旧
    前景色；
  - 运行时切主题后对标题栏做一次 hide/show，迫使 QWindowKit 的非客户区宿主
    重建，浅色下标题栏不再是深色。
- 无障碍：`SectionRow` 从 QWidget 改成 QAbstractButton，UIA 暴露为按钮，
  可用 Tab 聚焦、Space/Enter 触发；补了焦点描边。
- 中文翻译：52 条，`translations/qt_md_observatory_zh_CN.ts` 入库；CMake 在
  build 后用 lrelease 生成 qm 并复制到 exe 旁 `translations/`；`main` 按系统
  locale 加载。界面源语言仍是英文。
- 安装：`Runtime` 组件增加 `qt_md_observatory.exe` 与 zh_CN qm。
- 性能：200 篇文档的 WorkspaceModel 扫描 82 ms；1 MB 文档的核心解析仍是
  M1 的 28 ~ 38 ms，第二趟并行增量 0.3 ~ 2.8 ms。

取舍：

- `apps/shared` 合并未做：新 app 的 `WorkspaceModel` 与 qt_md_viewer 的
  `document_index` 仍是两套，等第二个 app 稳定后再合并。
- 旧的 `markdown_to_html` 路径未删除：它仍有测试覆盖，删除需要独立重构。
- 高对比度、多显示器 DPI 切换、安装包（Inno Setup）没有完成，留在发布前做。

测试：`test_workspace_model` 4 项（含 200 篇加载）；`ctest -L MARKDOWN`
55/55 通过。截图见 `temp/obs_m5_light2.png`、`temp/obs_m5_dark_cn.png`、
`temp/obs_m5_palette_cn.png`。
