# merman 预编二进制版本记录

| 日期 | merman | Rust | target | 产物 | SHA256 | 备注 |
| --- | --- | --- | --- | --- | --- | --- |
| 2026-10-22 | 0.9.2 | 1.95.0 (stable-msvc) | x86_64-pc-windows-msvc | bin/x64/merman_ffi.dll | 5cec12a118e1c1056ab068c1d7ee653c7e02bb8d57c7e9ed0edacc560b2d76e6 | VS2019 x64，release profile |
| 2026-10-22 | 0.9.2 | 1.95.0 (stable-msvc) | i686-pc-windows-msvc | bin/x86/merman_ffi.dll | 18e3a69e7d9e7a6ed0bf7f7cebfcba0f62f694bc93920bd05bbbe7579371ef7a | VS2019 x86，release profile |

构建命令（x64；x86 换 `--target i686-pc-windows-msvc` 并在
`vcvars32.bat` 环境中执行）：

```bash
cargo build -p merman-ffi --release --locked \
  --target x86_64-pc-windows-msvc \
  --no-default-features \
  --features svg,diagram-flowchart,diagram-sequence,diagram-class,\
diagram-state,diagram-er,diagram-gantt,diagram-pie,diagram-journey,\
diagram-git-graph,diagram-mindmap,diagram-timeline,\
diagram-quadrant-chart,diagram-requirement
```

- 不启用 `math`：Mermaid 内嵌公式不在范围，公式继续走 MathJax。
- 不启用 `layout-elk` / `layout-cytoscape`，避免 EPL-2.0 的 ELK。
- 头文件：`include/merman.h`、`include/merman_text_measurement_abi.h`、
  `include/merman_resource_contract.h`。
- 许可证：MIT OR Apache-2.0（本目录 `LICENSE-MIT` / `LICENSE-APACHE`）；
  第三方声明见 `THIRD_PARTY_NOTICES.md`。
- 运行依赖：VC++ 运行库（与 Qt 应用一致，安装包已带 vc_redist）。
- 升级流程见 `docs/guide/merman构建指南.md`。
