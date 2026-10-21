# AGENTS.md

本仓库的规则、命令与约定统一维护在 [CLAUDE.md](CLAUDE.md)，动手前先读它。

## 文档

| 文档 | 内容 |
| --- | --- |
| [README.md](README.md) | 项目定位、构建、安装、第三方库 |
| [README.zh-CN.md](README.zh-CN.md) | 中文说明 |
| [CONTRIBUTING.md](CONTRIBUTING.md) | 提交、测试与代码风格 |
| [docs/architecture.md](docs/architecture.md) | 分层与数据流 |
| [docs/rendering.md](docs/rendering.md) | Markdown 到 HTML、公式与 Mermaid |
| [docs/diagnostics.md](docs/diagnostics.md) | 诊断计数、日志与产物落盘 |
| [docs/eval/Markdown渲染qjsc预编译评估.md](docs/eval/Markdown渲染qjsc预编译评估.md) | qjsc 字节码评估数据 |
| [NOTICE](NOTICE) | 第三方许可证 |

## 快速约束

- 代码主要由 AI 编程助手在维护者思路与审查下生成；提交信息不加 AI
  共同作者署名。
- 构建、测试、格式化和 Git 规则以 [CLAUDE.md](CLAUDE.md) 为准。
- 不要静默修改 `3rdparty/`；补丁记录到 `patches/`。
- 改动 `src/markdown/` 后至少跑 `ctest -L MARKDOWN`。
