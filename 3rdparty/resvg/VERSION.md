# resvg 预编二进制版本记录

| 日期 | resvg | Rust | 产物 | SHA256 | 备注 |
| --- | --- | --- | --- | --- | --- |
| 2026-10-22 | 0.48.1 | 1.99.1 (stable-msvc) | bin/win-x64/resvg.dll | 08741e9bb21e66380b5678eace2e6d1cf65193cf98ef98eefe26758eaf5ff034 | VS2019 x64 工具链，release profile，默认 feature |
| 2026-10-22 | 0.48.1 | 1.99.1 (stable-msvc) | bin/win-x86/resvg.dll | 8ccb13e5184f5a783aac9933e4788aa1c213b18bccfe4228c49b68a27d5ea345 | VS2019 x86 工具链 + `--target i686-pc-windows-msvc`，release profile，默认 feature |
| 2026-10-22 | 0.48.1 | 1.99.1 (stable-msvc) | bin/win-x64/resvg.dll.lib | ed369e4e0567962a93652a0f2b608dc0bb17c09f2c71bb48055e0b36d56b58f7 | x64 import library |
| 2026-10-22 | 0.48.1 | 1.99.1 (stable-msvc) | bin/win-x86/resvg.dll.lib | f6a7c8fd6006e8686babd26e62deb414e0cd4060b39225f375e283ca7bcc1025 | x86 import library |
| 2026-10-23 | 0.48.1 | 1.99.1 | bin/linux-x64/resvg.so | 7a5c832e1efda0ec42e681f4b9142142d3c0d9a19f80941cce7a2d9389c7cdc4 | WSL x86_64-unknown-linux-gnu，`cargo build --release -p resvg-capi`，libresvg.so 重命名为 resvg.so |

构建与升级流程见 `docs/guide/resvg构建指南.md`。

- 头文件：`include/resvg.h`（随 resvg 源码分发，cbindgen 预生成）
- 许可证：Apache-2.0 OR MIT（本目录 LICENSE-APACHE / LICENSE-MIT）
- 运行依赖：VC++ 运行库（与 Qt 应用一致，安装包已带 vc_redist）
