# qlitemd Qt 控件层

本目录实现 Qt Widget 组件 `qlitemd::markdown_view`（构建目标
`qlitemd_markdown_view`）：`MarkdownView`、qlitehtml 封装、SVG 光栅化、
持久 PNG 缓存与主题。Markdown 解析、高亮、公式和 Mermaid 由
`src/mdhtml` 的零 Qt 核心完成；本层把核心的 SVG 资产重新映射成
`mj:` token 并 pin 成 QPixmap。

`md_viewer` 只是该组件的参考宿主。其它 Qt 工程通过
`add_subdirectory` 引入 qlitemd 源码并链接 `qlitemd::markdown_view`；
发布 CI 生成的 `qlitemd-<version>-qt-src.zip` 带齐所需源码和固定版本
依赖。Qt 版本和编译器由消费方决定。

架构与渲染说明见 `docs/architecture.md`、`docs/rendering.md`。
