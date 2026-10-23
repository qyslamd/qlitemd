# RaTeX 预编二进制版本记录

| 日期 | RaTeX | Rust | target | 产物 | SHA256 |
| --- | --- | --- | --- | --- | --- |
| 2026-10-23 | 0.1.15 | 1.90.0 | x86_64-pc-windows-msvc | bin/win-x64/ratex_ffi.dll | ae8315ac1aa91c03fc2e1e90817f2271db204d2898a0d468cfb4babbe9f19caa |
| 2026-10-23 | 0.1.15 | 1.90.0 | x86_64-pc-windows-msvc | bin/win-x64/ratex_ffi.dll.lib | 13e507c1a03798efabdcc24ed3096ba94fa56245c58d7030bafa387408c08a47 |
| 2026-10-23 | 0.1.15 | 1.90.0 | i686-pc-windows-msvc | bin/win-x86/ratex_ffi.dll | 0a297a47af78004a88b22307594b932d53038b7c7570dab6459e8ae613d10311 |
| 2026-10-23 | 0.1.15 | 1.90.0 | i686-pc-windows-msvc | bin/win-x86/ratex_ffi.dll.lib | 45ab2c91fe36bef1ecd77d03cc26b2b6b6d07e690d4c1c0237657de294af8ddb |
| 2026-10-23 | 0.1.15 | 1.99.1 | x86_64-unknown-linux-gnu | bin/linux-x64/ratex_ffi.so | 57fa23f45b0d072442acb8dfa93ef67112cf0479e6f936b6d6522fced02dd25e |

- 来源：https://github.com/erweixin/RaTeX v0.1.15（本地源码快照
  `D:\software\aa-sources\RaTeX-0.1.15`），MIT
- **本地补丁**：`patches/ratex/0001-add-svg-ffi.patch` 增加
  `ratex_render_svg` 与 `font_size`/`padding`/`stroke_width` 选项
- 构建命令（在对应 vcvars 环境中执行）：

```bat
cargo build --release --target x86_64-pc-windows-msvc -p ratex-ffi
cargo build --release --target i686-pc-windows-msvc -p ratex-ffi
```

Linux x64（在 WSL 中，产物名 `libratex_ffi.so`，入库为
`bin/linux-x64/ratex_ffi.so`）：

```bash
cargo build --release -p ratex-ffi
```

- 产物取 `target/<triple>/release/ratex_ffi.dll` 与 `ratex_ffi.dll.lib`
- 升级：重新应用补丁、重建、更新本表与 SHA256、跑 MARKDOWN 测试
