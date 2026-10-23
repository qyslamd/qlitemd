# CLAUDE.md

This file gives AI coding agents the rules, commands and constraints for
working in this repository.

## 项目概述

qlitemd 是一个独立的 Qt/C++ Markdown 渲染库，附带 FluentUI 风格的
`md_viewer` 示例。

渲染链路：

```text
Markdown -> md4c -> qlitehtml/litehtml -> QWidget
                  \-> tree-sitter: 语法高亮（原生）
                  \-> RaTeX: 公式（Windows 原生）/ QuickJS: MathJax（其它平台）
                  \-> merman-ffi: Mermaid（原生）
                  \-> resvg: 公式/图表 SVG 光栅化
```

- 主验证平台：Windows + MSVC
- 验证矩阵（以这两组为准）：
  - Windows：Qt 5.15.2 x86、Qt 6.8.3 x64
  - WSL：Qt 5.12.8 x64
- 开发期本地只跑 Qt 5.15.2 x86；其余组合在功能完成后交给 GitHub CI
- Qt 支持目标：Qt 5.12.x ~ Qt 6.12.x（当前实测覆盖以上矩阵）
- 原生后端：`QLITEMD_SVG_BACKEND=resvg|qtsvg`、
  `QLITEMD_MERMAID_BACKEND=merman|quickjs`；merman 要求 resvg。
  Windows 公式走 RaTeX（`3rdparty/ratex`），不编 QuickJS；其它平台
  回退 QuickJS + MathJax
- Linux：库与测试由 CI（Ubuntu 24.04 + Qt 6.8.3）覆盖；
  `md_viewer` 运行时尚未验证；原生后端目前仅 Windows
- 自有代码许可证：MIT；第三方许可见 `NOTICE`

## 关于作者与 AI

本仓库的代码主要由 AI 编程助手在维护者提供的思路、设计和约束下生成。
维护者负责需求、架构取舍、审查与最终验证。提交历史不添加 AI 共同作者
署名，也不使用默认的 Claude 署名。

### 为了避免 AI Agent 在 Windows 上和 `cmd` `powershell` `pwsh`斗智斗勇的说明
大多数的 AI Agent 默认习惯使用 bash 的环境来处理脚本。在 windows 上，通常只有 cmd 和旧版本的 powershell
因此。
- 默认编写 `pwsh` 脚本或者 `python` 脚本来处理
- 除非是参考资料给的文件，否则统一使用`UTF-8`编码
- MSVC编译器也统一增加编译器选项 /utf-8 
- 只要链接了 Qt 的 共享库的的可执行程序，在执行它们之前，必须这样做：
  设置和Qt构建版本相匹配的两个环境变量：
  - `PATH`:X:\path\to\qmake.exe\..\
  - `QT_PLUGIN_PATH`:X:\path\to\qmake.exe\..\plugins
  - 举个例子：
  ```bat
  @echo off
  setlocal

  set "PATH=D:/Qt22/5.15.2/msvc2019/bin;%PATH%"
  set "QT_PLUGIN_PATH=D:/Qt22/5.15.2/msvc2019/plugins"

  chcp 65001 >nul

  cd /d "D:\trae_workspace\qlitemd\build_qt515_resvg_x86\bin"

  rem Explicit .\ prefix: with NoDefaultCurrentDirectoryInExePath in effect,
  rem a bare filename is not resolved from the current directory.
  .\md_viewer.exe  %*
  ```

## 常用命令

### 构建

Windows 下先在 Visual Studio 开发者命令行中准备 MSVC 环境。
本地开发、验证与发布统一使用 `RelWithDebInfo`。

```bash
cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_PREFIX_PATH=/path/to/Qt/5.15.2/msvc2019
cmake --build build
```

Qt 6：

```bash
cmake -S . -B build-qt6 -G Ninja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DQLITEMD_QT_MAJOR=6 \
  -DCMAKE_PREFIX_PATH=/path/to/Qt/6.8.3/msvc2022_64
cmake --build build-qt6
```

也可以使用 `CMakePresets.json` 的 `qt5`、`qt6` 预设，并把 `QTDIR` 指向
目标 Qt Kit。

Linux/WSL（本地验证口径：Qt 5.12.8 x64；CI 为 Ubuntu 24.04 + Qt 6.8.3）：

```bash
cmake -S . -B build-linux -G Ninja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DQLITEMD_QT_MAJOR=6 \
  -DQLITEMD_BUILD_MD_VIEWER=OFF
cmake --build build-linux
QT_QPA_PLATFORM=offscreen ctest --test-dir build-linux --output-on-failure -L MARKDOWN
```

构建产物：

- `build/bin/md_viewer.exe`
- `build/bin/tests/test_*`

### 测试

```bash
ctest --test-dir build --output-on-failure -L MARKDOWN
```

- 改到 `src/markdown/` 时只跑 `MARKDOWN` 测试组，不要全量。
- Linux 无显示环境使用 `QT_QPA_PLATFORM=offscreen`。
- 行为变更必须补可无头运行的测试。

### 安装

```bash
cmake -S . -B build -DCMAKE_INSTALL_PREFIX=<prefix>
cmake --build build
cmake --install build
```

安装分为 `Runtime`、`Development`、`Licenses` 三个组件。`Runtime` 只含
`md_viewer`、FluentUI/ExWidgets DLL 和 Qt 运行时；`Development` 含静态库、
公开头文件和 `qlitemdConfig.cmake`；`Licenses` 含项目与第三方许可证。

```bash
cmake --install build --component Runtime
cmake --install build --component Development
cmake --install build --component Licenses
```

消费方：

```cmake
find_package(qlitemd CONFIG REQUIRED)
target_link_libraries(myapp PRIVATE qlitemd::markdown)
```

`examples/minimal` 是只依赖已安装 SDK 的最小 `QWidget` 示例；它不依赖
FluentUI/QWindowKit，也是 CI 的 install/consumer smoke 用例。

### 格式

```bash
clang-format -i <file>
clang-format --dry-run --Werror <file>
```

提交前，改动的 C++ 文件必须先过 `.clang-format`。头文件顺序敏感处要用
`// clang-format off` 包住。`clang-tidy` 默认不跑，除非用户明确要求。

## 代码结构

- `src/markdown/`：库主体，目标名 `qlitemd_markdown`
- `resources/`：主题 CSS 与 JS 载荷；JS 由 `qjsc` 在构建期编成字节码
- `examples/md_viewer/`：FluentUI + QWindowKit 示例（递归文档树、书签、
  全文搜索、会话恢复；逻辑拆在 `document_index` / `bookmarks` /
  `text_search` / `navigation_history` / `documents_panel`）
- `examples/minimal/`：面向已安装 SDK 的最小 `QWidget` 示例
- `tests/markdown/`：库的无头 GoogleTest 测试
- `tests/md_viewer/`：示例层模块与文档面板的无头测试
- `cmake/`：`qlitemdConfig.cmake.in` 与测试辅助
- `installer/`：Inno Setup 安装包脚本
- `licenses/`：JS 载荷的许可证文本
- `3rdparty/`：固定版本、已打补丁的依赖快照
- `patches/`：补丁来源记录
- `docs/`：架构、渲染、诊断和评估文档
- `CHANGELOG.md`：发布说明

## 代码规则

遵循 Google C++ Style Guide：

- C++17；C 代码使用 C11。
- 类型/函数/枚举用 `PascalCase`，变量用 `snake_case`，成员变量尾随
  下划线，常量用 `kPascalCase`，宏用 `ALL_CAPS`。
- 文件名用 `snake_case`，头文件用 `#define` 保护，禁止 `#pragma once`。
- 包含顺序：关联头文件、C 标准库、C++ 标准库、Qt、项目头文件。
- 行宽 80（注释/字符串可放宽到 100），缩进 2 空格，不使用 Tab。
- 指针/引用符号跟类型：`int* p`、`const QString& s`。
- 优先 `std::unique_ptr`，共享所有权用 `std::shared_ptr`，禁止裸
  `new/delete`。
- 禁止 `using namespace std`、RTTI（`dynamic_cast`）和 C 风格转型。
- 单行 `if`/`for` 也加大括号。
- 注释简洁，写“为什么”，不要写“做了什么”。
- 不要为了注释而注释；一段注释解释不清，通常说明代码结构有问题。

项目约定：

- 本项目不使用命名空间。
- 公开头文件不得暴露 qlitehtml、QuickJS 或 md4c 类型。
- 不在 C++ 中硬编码界面样式；主题 CSS 放在 `resources/themes/`，
  示例的 FluentUI 样式由 FluentUIStyle 负责。
- 代码中禁止 emoji；示例图标优先使用 `QStyle` 标准图标或新增 SVG。
- 不在 QObject 派生类中使用中文 `tr()`；界面文本用 `QStringLiteral`。
- 日志优先使用 `logging/log_core.h` 的 `LOG_*` 宏。
- QWidget 派生类把 UI 初始化和信号槽连接拆到 `initUi()` 与
  `initSignals()`。
- `md_viewer` 中 `FindText` 与 Windows 宏冲突：包含顺序敏感处必须
  关闭 clang-format 并 `#undef FindText`。

## 第三方依赖

- 不要静默修改 `3rdparty/`。改动要记录到 `patches/`，升级时同步
  `NOTICE` 和 README 的第三方库表。
- `3rdparty/fluentui/.gitignore` 忽略了 `/cmake`；新增的
  `*Config.cmake.in` 必须用 `git add -f` 强制加入，否则干净 checkout
  会在 configure 阶段失败。
- `3rdparty/fluentui` 有本地补丁（`patches/FluentUIStyle/`）：除 Qt5
  兼容补丁外，`0002-exwidgets-trim.patch` 增加
  `EXWIDGETS_MINIMAL` 与 `EXWIDGETS_ENABLE_SPECTRUM` 两个选项。顶层
  默认只编 md_viewer 用到的控件，kissfft 不再参与构建；升级 FluentUI
  时需重放补丁。
- `resources/` 下四份 JS 载荷按字节稳定，`.gitattributes` 标了 `-text`；
  不要做行尾转换，也不要直接编辑压缩后的文件。Windows 构建不再使用
  这些载荷（公式走 RaTeX、Mermaid 走 merman），它们只服务其它平台的
  quickjs 回退。
- `patches/**` 固定使用 LF。
- 预编原生库（resvg / merman / ratex）按 `<平台>-<架构>` 归档：
  `3rdparty/<lib>/bin/win-x64|win-x86|linux-x64|linux-arm64/`。
  Windows 只提供 x64/x86；Linux 目前 x64，后续考虑 arm64。
  CMake 用 `QLITEMD_PLATFORM_DIR` 选择目录，并在该平台缺产物时
  自动回退到 quickjs/qtsvg。

## Git 规则

- 使用 Conventional Commits：`<type>(<scope>): <subject>`。
- subject 用中文祈使句，不超过 50 字，结尾不加句号。
- body 解释为什么，每行不超过 72 字符。
- 禁止 `Co-Authored-By`、Claude 署名和提交信息首尾的 `@`。
- 所有会修改 git 仓库的命令都必须先得到维护者确认；`git status`、
  `git log` 等只读命令不受限制。
- 改动完成并编译通过后，不要擅自提交；用户明确要求提交时再提交。

## 已知坑

- 改了 CMake `option()` 默认值后，已有构建目录不会跟随；用
  `cmake -B <build> -U <option>` 或删对应缓存项，不要直接删整个构建目录。
- 干净 checkout 可能缺少被 vendored `.gitignore` 忽略的源文件；CI 失败
  先检查 `git ls-files` 是否包含必要的 CMake 模板。
- Qt 6 移除了 `QSettings::setIniCodec`；Qt5/Qt6 共用代码要加版本保护。
- 最低 Qt 版本已降到 5.12：WSL/Ubuntu Focal 的 Qt 5.12.8 实测可构建
  库、测试与 `md_viewer`（两种后端都过）。Linux CI 仍使用 Qt 6.8.3。
- 本机 GitHub HTTPS 可能连不上，SSH 可用；CI 的 Qt 安装在 GitHub
  Runner 上，不使用本机 Qt。
- `.bat` 必须 CRLF，`.sh` 必须 LF；JS 载荷和补丁文件按
  `.gitattributes` 保持字节稳定。
- `md_viewer` 不分发 Segoe Fluent Icons。FluentUI3Style 与
  FluentTitleBar 必须按 `Segoe Fluent Icons` -> `Segoe MDL2 Assets`
  的顺序使用系统字体；不要改成 `QFont()` 的默认族，否则图标会变成
  方框。

## 发布

- 版本号写在顶层 `project(qlitemd VERSION x.y.z ...)`；tag 用 `vx.y.z`，
  发布 workflow 会校验两者一致。
- 推送 `v*` tag 触发 `.github/workflows/release.yml`：Qt 5.15.2 x64
  构建、跑测试、安装 `Runtime + Licenses`、用 Inno Setup 生成安装包，
  并把 exe 对应的 PDB 打成 zip。
- 安装包脚本是 `installer/md_viewer.iss`；使用 Inno Setup 6.x。
- 发布产物：`qlitemd-md_viewer-<version>-x64-setup.exe` 和
  `qlitemd-md_viewer-<version>-x64-pdb.zip`。
- 发布构建固定 Rust 后端：`resvg + ratex + merman`，不打 QuickJS。

## 变更流程

- 非平凡改动先讨论设计，再写代码。
- 新增功能或行为变化要补无头测试，并同步文档。
- 修改公开 API、安装规则或第三方依赖时，更新 README、NOTICE 和
  `docs/` 中对应说明。
- 发布使用 `RelWithDebInfo`，保留 PDB。
