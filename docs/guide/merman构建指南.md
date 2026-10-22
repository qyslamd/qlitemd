# merman 构建指南

qlitemd 以**预编二进制**方式使用 merman-ffi（`3rdparty/merman/` 下的
`merman_ffi.dll`、import lib 与头文件），日常开发、CI 与终端用户不需要
Rust。本文档只服务一种场景：**升级 merman 版本时，维护者重建 DLL**。

这是维护者自用指南；自行从源码构建 merman 遇到的问题不属于 qlitemd
的支持范围，请向上游反馈。

## 一、环境搭建（一次性）

1. **VS x64/x86 工具链**：x64 在 VS Native Tools x64 命令行中构建，
   x86 在 `vcvars32` 中构建。Rust 的 MSVC 链接阶段依赖该环境。
2. **Rust**：merman 0.9.2 的 `rust-toolchain.toml` 钉定 **1.95.0**；
   rustup 会自动选择该 toolchain。
3. **x86 target**：钉定 toolchain 默认只装 x64 std，需要单独补：

   ```bat
   rustup target add i686-pc-windows-msvc --toolchain 1.95.0-x86_64-pc-windows-msvc
   ```

4. merman 源码根需要存在 `Cargo.lock`；构建命令带 `--locked`，避免
   依赖漂移。

## 二、构建

feature 使用 qlitemd 的显式子集，不启用 `math`、`all-diagrams`、
`layout-elk`、`layout-cytoscape`：

```text
svg
diagram-flowchart
diagram-sequence
diagram-class
diagram-state
diagram-er
diagram-gantt
diagram-pie
diagram-journey
diagram-git-graph
diagram-mindmap
diagram-timeline
diagram-quadrant-chart
diagram-requirement
```

x64：

```bat
cd /d D:\path\to\merman-<版本>
cargo build -p merman-ffi --release --locked ^
  --target x86_64-pc-windows-msvc ^
  --no-default-features ^
  --features svg,diagram-flowchart,diagram-sequence,diagram-class,diagram-state,diagram-er,diagram-gantt,diagram-pie,diagram-journey,diagram-git-graph,diagram-mindmap,diagram-timeline,diagram-quadrant-chart,diagram-requirement
```

x86：

```bat
cd /d D:\path\to\merman-<版本>
cargo build -p merman-ffi --release --locked ^
  --target i686-pc-windows-msvc ^
  --no-default-features ^
  --features svg,diagram-flowchart,diagram-sequence,diagram-class,diagram-state,diagram-er,diagram-gantt,diagram-pie,diagram-journey,diagram-git-graph,diagram-mindmap,diagram-timeline,diagram-quadrant-chart,diagram-requirement
```

产物：

```text
target\x86_64-pc-windows-msvc\release\merman_ffi.dll
target\x86_64-pc-windows-msvc\release\merman_ffi.dll.lib
target\i686-pc-windows-msvc\release\merman_ffi.dll
target\i686-pc-windows-msvc\release\merman_ffi.dll.lib
```

`merman_ffi.lib` 是体积很大的静态库，qlitemd 不用；只取
`merman_ffi.dll` 与 `merman_ffi.dll.lib`。

## 三、产物校验清单

```bat
:: 架构：x64 为 8664，x86 为 14c
dumpbin /headers target\x86_64-pc-windows-msvc\release\merman_ffi.dll | findstr machine

:: 导出入口
dumpbin /exports target\x86_64-pc-windows-msvc\release\merman_ffi.dll | findstr merman_get_native_api
```

size 参考：当前子集 x64 约 16.7 MB，x86 约 13.2 MB。
明显偏小通常是 feature 列表漏项；明显偏大要检查是否误开了
`all-diagrams` 或 `layout-elk`。

## 四、入库与升级流程

1. 按第二节构建 x64 与 x86；
2. 替换 `3rdparty/merman/` 下的 DLL 与 import lib；
3. 同步 `crates/merman-ffi/include/` 的三个头文件；
4. 更新 `3rdparty/merman/VERSION.md`：merman 版本、Rust 版本、target、
   构建日期、feature、SHA256；
5. 核对 `LICENSE-MIT` / `LICENSE-APACHE` / `THIRD_PARTY_NOTICES.md`；
6. 跑 `ctest -L MARKDOWN`，再用 `MD_MARKDOWN_DUMP` 的产物批量比对；
7. 注意 x86 ABI 的 8 字节对齐要求：集成代码里
   `MermanNativeEngineToken` 与 `MermanNativeResult` 必须 `alignas(8)`。

## 五、明确不做的事

- 不在默认 CMake 构建或 CI 中安装 Rust、调用 cargo；
- 不把 `target/` 或 `vendor/` 入库；
- 不启用 `math`（公式继续走 MathJax），也不启用 ELK/Cytoscape；
- 不回答“我的 Rust 环境编不过”这类问题——参照第一节自建，或直接
  使用仓库内的预编 DLL。
