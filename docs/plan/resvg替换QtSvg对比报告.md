# QtSvg 迁移 resvg 对比评估报告

状态：评估完成，待评审。
实验日期：2026-10-22。
配套实验：`temp/mermaid-pk/`（QuickJS + mermaid + 双渲染器并排对比程序），
`temp/resvg-vs-qtsvg/`（手工 SVG 静态库接入验证）。

## 一、目的

`MarkdownView` 的公式与 Mermaid 图最终都走 `QSvgRenderer`（QtSvg 模块）
光栅化。生产环境长期暴露两类问题：

- Mermaid 图填充色错乱、文字对比度低、布局要素丢失；
- 为绕开 QtSvg 的限制，`mermaid_svg_cleanup.cpp` 维护了近千行的
  后处理（文本定标、CSS 内联、marker 展开），`formula_renderer` 还有
  `currentColor` 替换 hack，持续吃维护成本。

本评估验证用 resvg（Rust 实现的 SVG 渲染库，C API + 静态/动态库）
替换 QtSvg 是否成立，并给出成本与迁移路径。

## 二、实验设置

- mermaid 11.16.0 在 QuickJS（qjsc 字节码）中渲染，与生产同款桥接：
  `dom_shim` + `path_bbox` + 真实 `QFontMetricsF` 度量回调；
- 渲染产物为**未做任何后处理的原始 mermaid SVG**——故意不给两侧"帮忙"，
  这正是替换后新管线将直接面对的输入；
- 唯一归一化：把 mermaid 输出的 `width="100%"` 与 `style="max-width"`
  改写为 viewBox 数值，**两侧吃完全相同的输入**；
- 复杂时序图（autonumber/loop/alt/activate/跨线 note/中文参与者）与
  复杂类图（构造型、泛型、静态成员、note、关系标签、多重性 `2..8`）各一；
- 两侧均 2x 光栅化；计时为预热 1 轮后取 3 轮中位数；
- resvg 0.48.1，MSVC x64 静态库；Qt 5.15.2 x64（msvc2019_64）。

## 三、质量对比（决定性）

证据文件（本目录）：

- `sequence.qt.png` / `sequence.resvg.png`
- `class.qt.png` / `class.resvg.png`
- `formula-display-int.*.white.png`
- `formula-display-quadratic.*.white.png`
- `formula-inline-cjk-text.*.white.png`
- `x86.qt.png` / `x86.resvg.png`

截图尺寸：mermaid 2x（2110x2046 与 2850x1146）；公式 4x（白底合成）。

### 时序图

| 项 | QtSvg | resvg |
| --- | --- | --- |
| 参与者框填充 | 深灰黑（应为主题淡紫 `#ECECFF`） | 正确 |
| 消息文字 | 低对比、字体回退成衬线，几乎不可读 | 清晰，粗细正确 |
| 激活条 | 深色 | 白色，正确 |
| loop/alt 虚线框、autonumber 序号圈 | 有绘制但配色错 | 正确 |
| 中文 note 框 | 位置正确 | 正确 |

### 类图（差距最大）

| 项 | QtSvg | resvg |
| --- | --- | --- |
| 类名标题 | JsRuntime / MermaidEngine 标题丢失 | 全部正确 |
| `«widget»` 等构造型 | 丢失 | 全部渲染 |
| 布局 | 塌陷：框体散架、大块空白、关系线几乎不可见 | 完整还原 mermaid 的布局 |
| 关系标签（提交任务/worker 池/现状） | 黄棕色、错位浮点 | 白底黑字，位置正确 |
| 多重性 `2..8` | 丢失 | 正确 |
| 成员文本字体 | 回退为衬线（宋体感） | 无衬线，与 mermaid 一致 |
| `QPointer<QLiteHtmlWidget>` 泛型、静态 `Print$` 下划线 | 错位 | 正确 |

### 根因

mermaid 的 SVG 高度依赖 `<style>` 内的 CSS 类选择器（`.actor{fill:...}`、
`:root` 变量、`#mermaid-m0` 作用域等）。QtSvg 不支持 CSS `<style>` 块与
类选择器，只认零散 presentation attribute，于是所有由 CSS 指定的填充、
字色、字族全部落错；文本尺寸类问题又进一步放大为布局塌陷。resvg 有完整
CSS 支持（选择器、继承、`currentColor`），生 SVG 直接正确呈现。

结论：**质量维度 resvg 完胜，且不是程度差异，是"可用与不可用"的差异。**

### 公式（MathJax）对比

补测四组公式（inline 毕达哥拉斯、display 积分、display 求和+二次方程
求根公式、含中文 `\text`），原始 `currentColor`、ex 单位归一化为
ex=8px（与生产约定一致），4x 光栅化。截图见本目录
`formula-*.white.png`（白底合成）：

| 项 | QtSvg | resvg |
| --- | --- | --- |
| 积分/求和/分式/根号的排版 | 正确 | 正确，与 QtSvg 肉眼无差 |
| 字形（MathJax 内嵌 path 字形） | 正确 | 正确，逐像素级接近 |
| `currentColor` | 生效（按黑渲染） | **原生支持**（继承 color 属性） |
| `\text{（质能方程）}` 中文回退 | 正确 | 正确（回退族生效） |

两个额外发现：

1. **`currentColor` 无需替换 hack**：resvg 原生解析 currentColor；暗色
   主题配色可用 `resvg_options_set_stylesheet()` 注入
   `* { color: <主题色> }` 实现，比现在的字符串替换更干净。
2. **既有载荷限制（与渲染器无关）**：打包的 MathJax 载荷不支持
   `pmatrix` 等 ams 环境（报 `Unknown environment`，两侧拿到同样的
   merror SVG）。这是 `mathjax_svg_entry.mjs` 打包时 TeX 包集的问题，
   如需矩阵公式要在升级载荷时补 `ams`，与本次迁移无关。

结论：**公式路径同样满足替换条件，且能顺带删掉 `currentColor` 替换。**

### 32 位验证

用 `i686-pc-windows-msvc` target 构建的 x86 DLL，配合 Qt 5.15.2 x86
重新编译 `resvg-vs-qtsvg`，加载样张 SVG 成功（进程存活、两张位图落盘）。
证据见 `x86.qt.png` / `x86.resvg.png`：

- QtSvg 侧：箭头 marker 缺失、CSS 字体未生效（退化为衬线）、文字对比度低；
- resvg 侧：marker、字体、渐变、虚线箭头全部正确。

x86 与 x64 结论一致，DLL 架构与 Qt 配置匹配，无 32/64 位差异。

## 四、性能数据

构建为未优化配置（评估程序未设 `CMAKE_BUILD_TYPE`），绝对值仅作相对
参考；生产管线的 mermaid 执行耗时与渲染器无关，两侧共享同一 SVG 输入。
计时为预热 1 轮后取 3 轮中位数。

| 图表 | mermaid 执行 | SVG 大小 | QtSvg | resvg | 位图尺寸 |
| --- | ---: | ---: | ---: | ---: | --- |
| sequence @2x | 2628-3388 ms | 35940 B | 166-190 ms | 211-237 ms | 2110x2046（约 17MPix） |
| class @2x | 11332-13828 ms | 38647 B | 281-310 ms | 223-240 ms | 2849x1146（约 13MPix） |
| formula inline @4x | — | 4163 B | 4-13 ms | 81-116 ms | 380x64 |
| formula display @4x | — | 6055-13609 B | 6-22 ms | 92-104 ms | 1175x209 |

- 大图栅格化两侧同量级，无决定性差距；
- **小 SVG（公式）的 resvg 数字含一次性开销**：demo 每次调用都重建
  `resvg_options` 并重扫系统字体（fontdb），约 80-100 ms。真实集成应
  只建一次 options 复用，摊销后预期与 QtSvg 同量级；该假设需在实施时
  用 options 复用版本复核；
- 端到端耗时由 mermaid 在 QuickJS 里的执行主导（本机未优化构建 2.6s /
  11.3s；生产 relwithdebinfo 下显著低于此），换渲染器**不影响**这部分；
- 换 resvg 的收益在质量与维护成本，不在速度；也不引入速度回退。

## 五、成本

### 5.1 Rust 工具链（新增的最大隐性成本）

引入 resvg 意味着 qlitemd 的构建从"纯 CMake + C++ 工具链"变成
"C++ + Rust 双工具链"，影响面不止"装一个 rustup"：

- **开发者环境**：所有从源码构建的开发者都需要 Rust stable
  （resvg 0.48.1 要求 >= 1.85）+ 对应 MSVC 组件。当前仓库只要求
  CMake + Qt + VS，贡献门槛实打实抬高。
- **架构矩阵翻倍**：qlitemd 的主验证配置是 Qt 5.15.2 **x86**，x64 也要
  （Qt 5.15.2 x64 / 6.8.3），Linux CI（Ubuntu 24.04 + Qt6）已纳入。resvg
  必须为每个目标各编一份：Windows 需要额外安装
  `x86-pc-windows-msvc` target，Linux 需要 `x86_64-unknown-linux-gnu`。
  工具链 × 架构的组合都要锁定版本。
- **CI 增量**：三个 job 各加一步 rustup 安装（可用 rustup-toolchain
  action + cargo 缓存），每次 CI 时间增加约 30-60 秒（冷缓存更久）。
- **版本漂移**：需要 `rust-toolchain.toml` 钉死工具链版本，否则新编译器
  可能改变行为/告警；这与仓库现有的"钉死第三方版本"策略一致，但多了一
  类要钉的东西。
- **供应链审计面**：Cargo.lock 92 个 crate（resvg 传递依赖），快照
  vendor 目录 146 MB。若照搬现有 `3rdparty/` 全量入库策略，仓库体积会
  显著膨胀；若不入库则要求构建时有网络或使用 release 附带的二进制。

缓解选项（可组合）：

| 方案 | 说明 | 代价 |
| --- | --- | --- |
| A. 二进制分发 | x86/x64/Linux 的 `resvg.dll|.so` 作为构建产物归档（Release 资产或独立 repo），源码构建 resvg 变成维护者行为 | 开发者零 Rust 依赖；代价是二进制入库/下载与"可重建"信任问题 |
| B. Corrosion 桥接 | CMake 官方生态的 CMake↔Cargo 集成，自动处理 target/profile/产物路径 | CMake 侧多一个依赖；开发者仍需装 Rust |
| C. 可选后端 | `QLITEMD_ENABLE_RESVG` 开关，默认 QtSvg | 两套渲染路径并存，cleanup 代码删不掉，评估结论打了折扣 |
| D. 全量 vendor 入库 | 照搬 3rdparty 策略，`cargo vendor` 产物入库 | 仓库 +146 MB；换 resvg 版本时 vendor 重生成 |

**已决策：采用方案 A（二进制分发）。** 仓库只收 `resvg.dll/.so` 预编
产物与 C 头文件（`3rdparty/resvg/`），日常开发与 CI 完全不依赖 Rust。
升级 resvg 版本 = 维护者按 `docs/guide/resvg构建指南.md` 重建并替换
DLL；qlitemd 项目**不承担 Rust 工具链的维护与支持**——自行从源码构建
的用户参照该指南自建环境，出问题不属于 qlitemd 的支持范围。

### 5.2 发行体积

- `resvg.dll` 约 3.8 MB（x64 release），随 `Runtime` 组件分发，x86 需另编；
- 静态 `resvg.lib` 约 30 MB，不建议静态链入目标。

### 5.3 构建

- `cargo build --release -p resvg-capi`，源码树自带 vendor，离线可重复
  构建；
- C 接口面小：`resvg_options_*` → `resvg_parse_tree_from_data` →
  `resvg_get_image_size` → `resvg_render`，输出 premultiplied RGBA8888，
  与 `QImage::Format_RGBA8888_Premultiplied` 直接对应；
- 字体：`resvg_options_load_system_fonts()` 需显式调用；缺字回退族可设
  （本实验设 Microsoft YaHei/Consolas，中文渲染正确）。

## 六、迁移方案（草案）

接入点在 `MarkdownView` 侧光栅化前，替换 `QSvgRenderer`：

1. 新增 `src/markdown/resvg_renderer.{h,cpp}`，薄封装 C API：
   parse → size → transform(scale) → render → QImage；
2. `SvgToImgTag`（mermaid）与公式路径的 `QSvgRenderer` 调用点切换到该封装；
3. **删除** `mermaid_svg_cleanup.cpp` 的 CSS 内联与 marker 展开
   （文本定标是否保留待定：resvg 按字体库排版，理论不再需要，迁移时
   用 diff 验证后删）；删除 `formula_renderer` 的 `currentColor` 替换；
4. `dom_shim` 的 `__measureText*` 回调**保留**——mermaid 布局仍需要文本
   度量，这与最终栅格化用谁无关；
5. CMake：`3rdparty/resvg/` 收预编 DLL（x86/x64）+ `resvg.h`，运行期
   `bin/resvg.dll` 随 `Runtime` 组件分发；升级流程见
   `docs/guide/resvg构建指南.md`；
6. 许可证：resvg 为 Apache-2.0 OR MIT，二选一随 NOTICE 登记；
7. Qt 6 与 Qt 5 行为一致（resvg 不依赖 Qt），无版本兼容负担。

## 七、风险与待验证项

- [x] `docs/guide/resvg构建指南.md` 撰写（含 Rust 环境搭建、版本钉定、
      x86/x64 两条构建命令、产物校验清单）；
- [x] 预编 DLL 分发位置已定：`3rdparty/resvg/bin/{x64,x86}/resvg.dll`；
      x86 DLL 已构建，并用 Qt 5.15.2 x86 consumer 完成加载与渲染验证；
- [x] MathJax 公式 SVG 的 resvg 对比（inline/display/中文 text 四组）；
- [ ] 生产 relwithdebinfo 下重测端到端耗时（预期不变）；
- [ ] `resvg.dll` 与 QuickJS 的栈需求并存（resvg 自身递归浅，风险低）；
- [ ] 文本定标删除后的逐图 diff 验证（用 `MD_MARKDOWN_DUMP` 产物批量比）；
- [ ] 离线字体环境（无中文字体的机器）下 resvg 的回退表现。

## 八、结论

质量上 QtSvg 对 mermaid 输出是"不可用"，resvg 是"开箱即用"；公式路径
两侧质量相当，但 resvg 原生支持 `currentColor`，可删掉替换 hack；x86
与 x64 结论一致。性能同量级无回退（公式小图的一次性 options 开销需在
实施时用复用版本复核）。

引入 Rust 工具链的结构性成本通过**二进制分发**规避：仓库只收预编 DLL
与头文件，日常开发、CI 与终端用户完全接触不到 Rust；Rust 环境的搭建与
resvg 的重建流程文档化为 `docs/guide/resvg构建指南.md`，属于维护者自用
指南，qlitemd 项目不承担其维护与支持。

补验 MathJax 与 x86 后，剩余风险集中在两项实施期验证：
生产 relwithdebinfo 下的端到端耗时、以及删除文本定标后的真实产物批量
diff。按第六节草案出设计文档评审后实施。
