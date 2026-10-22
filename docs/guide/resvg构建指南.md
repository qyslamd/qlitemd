# resvg 构建指南

qlitemd 以**预编二进制**方式使用 resvg（`3rdparty/resvg/` 下的
`resvg.dll` 与 `resvg.h`），日常开发、CI 与终端用户不需要 Rust。
本文档只服务一种场景：**升级 resvg 版本时，维护者重建 DLL**。

这是维护者自用指南；自行从源码构建 resvg 遇到的问题不属于 qlitemd
的支持范围，请向上游（https://github.com/linebender/resvg）反馈。

## 一、环境搭建（一次性）

1. **VS x64 工具链**：机器上已有 VS2019/VS2022 即可，构建必须在其
   x64 Native Tools 命令行中进行（Rust 的 `link.exe` 依赖 MSVC 环境；
   在普通 Git Bash 里跑会命中 Git 自带的 GNU link.exe，报
   `extra operand` 错误）。
2. **Rust**：安装 rustup（https://rustup.rs），选 `stable-x86_64-pc-windows-msvc`
   默认工具链。resvg 0.48.1 要求 **Rust >= 1.85**。
3. **x86 target**（如需编 32 位 DLL）：

   ```bat
   rustup target add i686-pc-windows-msvc
   ```

4. 版本钉定：在 resvg 源码根放 `rust-toolchain.toml`（可选但推荐）：

   ```toml
   [toolchain]
   channel = "1.85.0"
   ```

   qlitemd 侧不依赖该文件，仅用于保证维护者机器上的可重复构建。

## 二、构建

官方 release 压缩包（`resvg-<版本>.tar.xz`）已自带 `vendor/` 与
`.cargo/config`，**离线可构建**，无需网络拉 crate。

x64（qlitemd 主配置）：

```bat
:: VS x64 Native Tools 命令行
cd /d D:\path\to\resvg-<版本>
cargo build --release -p resvg-capi
:: 产物：target\release\resvg.dll
```

x86（Qt 5.15.2 32 位配置用）：

```bat
:: VS x86 Native Tools 命令行（vcvars32）
cd /d D:\path\to\resvg-<版本>
cargo build --release --target i686-pc-windows-msvc -p resvg-capi
:: 产物：target\i686-pc-windows-msvc\release\resvg.dll
```

注意：

- `default-members` 只有 CLI，**必须带 `-p resvg-capi`** 才会编 C 库；
- cdylib 会同时生成 `resvg.dll` 与 `resvg.dll.lib`（import library），
  两者都要入库；
- `--target` 形式的产物在 `target\<三元组>\release\` 下，与宿主构建
  目录分开；
- 别动默认 feature（`text`/`system-fonts` 必须在），mermaid SVG 的
  `<text>` 依赖它们。

## 三、产物校验清单

```bat
:: 1. 架构正确（x64 应为 8664，x86 应为 14c）
dumpbin /headers target\release\resvg.dll | findstr machine

:: 2. 导出符号存在
dumpbin /exports target\release\resvg.dll | findstr resvg_render

:: 3. CLI 冒烟（同目录有 resvg.exe）
resvg.exe smoke.svg out.png
```

size 参考：x64 release 约 3.8 MB；明显偏小（如 < 2 MB）通常是 feature
被关了，回查 `crates/c-api/Cargo.toml` 的 default features。

## 四、入库与升级流程

1. 下载新版本 release 压缩包，校验 SHA256（GitHub release 提供 digest）；
2. 按第二节构建 x64 与 x86 两个 `resvg.dll`，按第三节校验；
3. 替换 `3rdparty/resvg/` 下的 DLL、`resvg.dll.lib` 与 `resvg.h`
   （同版本），在 `3rdparty/resvg/VERSION.md` 记录：resvg 版本、
   Rust 版本、构建日期、SHA256；
4. NOTICE 的 resvg 条目核对许可证（Apache-2.0 OR MIT）；
5. 跑 `ctest -L MARKDOWN`，再用 `MD_MARKDOWN_DUMP` 的产物批量比对
   渲染结果无回退；
6. 更新 CHANGELOG，走常规提交。

## 五、明确不做的事

- 不在 CI 中安装 Rust 或构建 resvg；
- 不把 `vendor/`（约 146 MB）或 `target/` 入库；
- 不回答"我的 Rust 环境编不过"这类问题——参照第一节自建，或直接使用
  仓库内的预编 DLL。
