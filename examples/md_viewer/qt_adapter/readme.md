# md_viewer Qt 适配层

本目录实现 `md_viewer` 的 Qt 适配层（静态库 `qlitemd_markdown`）：
`MarkdownView`、qlitehtml 封装、SVG 光栅化、持久 PNG 缓存与主题。
Markdown 解析、高亮、公式和 Mermaid 由 `src/mdhtml` 的零 Qt 核心完成；
适配层把核心的 SVG 资产重新映射成 `mj:` token 并 pin 成 QPixmap。

该目标只服务示例与本目录 `tests/` 的 gtest 用例，不进入安装包。
架构与渲染说明见 `docs/architecture.md`、`docs/rendering.md`。
