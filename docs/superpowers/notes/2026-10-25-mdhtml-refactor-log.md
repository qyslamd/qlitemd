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
