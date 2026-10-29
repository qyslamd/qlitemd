# qlitemd

**A Qt Widgets Markdown renderer that doesn't need a browser engine.**

md4c parses the document, qlitehtml lays it out, and formulas render
through a runtime-selectable engine (native RaTeX where its prebuilt library
exists, with a per-formula MathJax fallback in quickjs-ng).
Highlighting uses native tree-sitter, Mermaid diagrams render through a
runtime-selectable engine (native merman-ffi with a per-diagram mermaid.js
fallback, or either engine pinned), and
formula/diagram SVGs are rasterized with resvg. There is no WebEngine,
Chromium or Node.js at runtime.

[![Build](https://github.com/qyslamd/qlitemd/actions/workflows/build.yml/badge.svg?branch=main)](https://github.com/qyslamd/qlitemd/actions/workflows/build.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
[![Qt](https://img.shields.io/badge/Qt-5.14%2B-green.svg)](#requirements)
[![C++17](https://img.shields.io/badge/C%2B%2B-17-blue.svg)](#requirements)

[中文](README.zh-CN.md) · [Example](examples/md_viewer) · [Docs](docs/)

![qlitemd md_viewer screenshot](docs/images/screenshot.png)

## Why qlitemd?

Embedding Markdown preview in a Qt application usually means one of two
trade-offs:

- **QtWebEngine** is capable, but it brings Chromium, a large runtime and a
  heavy cold start.
- **QTextDocument** is light, but its Markdown and CSS support stops short of
  formulas, Mermaid diagrams and full HTML/CSS layout.

qlitemd takes a third path: a native `QWidget` backed by qlitehtml, with
JavaScript only where it is needed to produce SVG for formulas, diagrams and
syntax highlighting. The public header does not expose qlitehtml, QuickJS or
md4c types, so the rendering backend can be replaced without changing callers.

## Quick start

```cmake
add_subdirectory(path/to/qlitemd)
target_link_libraries(myapp PRIVATE qlitemd_mdhtml)
```

Or, from an installed SDK:

```cmake
find_package(qlitemd CONFIG REQUIRED)
target_link_libraries(myapp PRIVATE qlitemd::mdhtml)
```

```cpp
#include "md_pipeline.h"

MdHtmlPipeline pipeline;
const MdHtmlResult result = pipeline.Render(markdown_utf8);
// result.html + result.assets (formula/Mermaid SVG) + outline
```

The installed SDK is the zero-Qt `mdhtml` core: parsing and assembly on a
coordinator thread, highlight/formula/Mermaid on three worker lanes, and
SVG-only assets. The Qt widget (`MarkdownView`, qlitehtml, resvg/QtSvg
rasterization) lives in [`src/markdown_view`](src/markdown_view) and is
consumed from source, so the consumer owns the Qt version and compiler:

```cmake
set(QLITEMD_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(QLITEMD_BUILD_MD_VIEWER OFF CACHE BOOL "" FORCE)
set(QLITEMD_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(path/to/qlitemd)
target_link_libraries(myapp PRIVATE qlitemd::markdown_view)
```

Release assets include `qlitemd-<version>-qt-src.zip` with the core and
widget sources plus pinned dependencies, and a compiled core SDK for hosts
that only need `qlitemd::mdhtml` or `qlitemd::mdhtml_c`.

When embedding, qlitemd defines the vendored targets `md4c`, `qlitehtml`,
`litehtml`, `qjs`, `qjsc`, `tree_sitter_core` and `tree_sitter_grammars`;
a host that already defines the same target names should consume the source
package in an isolated subproject.

See [`docs/qt-widget.md`](docs/qt-widget.md) for the widget integration
details. [`examples/qt_widget_minimal`](examples/qt_widget_minimal) is the
`add_subdirectory` smoke example. A standalone zero-Qt console example is in
[`examples/minimal`](examples/minimal); it builds against an installed SDK.

## Features

- **GFM Markdown** — tables, task lists, strikethrough, autolinks,
  callouts/admonitions and footnotes through md4c; MPE callouts are the
  default, GitHub admonitions remain selectable per render.
- **Syntax highlighting** — native tree-sitter with 12 grammar languages
  (bash, batch, c, cpp, css, go, html, java, javascript, json, python,
  rust); the example renders per-line spans so line numbers stay aligned.
- **Formulas** — native RaTeX or MathJax in QuickJS emits SVG for inline
  `$...$` and display `$$...$$`, with theme-aware colors. The render option
  picks the engine; `auto` retries a failed formula with MathJax.
- **Mermaid diagrams** — both engines compile in: native merman-ffi (fast)
  and mermaid.js in QuickJS (broadest syntax coverage). The render option
  picks one per render; `auto` retries a failed diagram with mermaid.js.
- **SVG assets** — the core never rasterizes. `md_viewer` uses resvg, with
  the legacy QtSvg backend still selectable.
- **Themes** — built-in light and dark CSS; `SetThemeStyleSheet()` lets a host
  supply per-theme CSS, `SetPageBackground()` controls the page fill
  (including transparency for Mica/acrylic windows), and `MD_MARKDOWN_CSS`
  remains a live-tweak override.
- **Code block chrome** — language label, copy link and optional line numbers.
- **Outline** — `MarkdownHeading` list with stable slugs, ready for a side
  panel and anchor navigation.
- **Find** — case-sensitive, whole-word and backward search with wrap
  detection.
- **Zoom** — Ctrl+wheel or the built-in actions, 0.3x to 5.0x, debounced
  re-layout.
- **Async rendering** — per-engine worker pools, request-id filtering and
  cancel-on-navigate.
- **Three-tier cache** — in-memory HTML cache, SHA-256 keyed disk cache and a
  cross-document QPixmap cache.
- **Export** — HTML with a companion `_files/` asset directory, and PDF
  through a host-provided `QPrinter`.
- **Diagnostics** — the `qt.markdown.render` logging category, per-stage
  timings and optional artifact dumps (`MD_MARKDOWN_DUMP`).

## Architecture

```text
Markdown source
      |
      v
md4c  ->  HTML skeleton + formula/Mermaid placeholders
      |
      v
MarkdownRenderService (worker pools)
      |-- tree-sitter   ->  highlighted HTML          (native)
      |-- RaTeX/MathJax ->  SVG                       (native / QuickJS)
      `-- mermaid.js    ->  SVG   (QuickJS; native merman selectable per render)
      |
      v
assemble HTML  ->  qlitehtml widget  ->  painted QWidget
```

Key design choices:

- **Dependency inversion.** The public API knows nothing about QuickJS,
  merman or resvg. Highlighting, formula and Mermaid work are passed in as
  plain callbacks, so tests can use fakes and hosts can swap engines.
- **Native engines, QuickJS fallback.** Formulas and Mermaid render with
  native RaTeX/merman where available and fall back to MathJax/mermaid.js
  per item, so QuickJS is always part of the build.
  `QLITEMD_SVG_BACKEND=resvg` stays the default rasterizer choice for the
  example.
- **Bytecode for the JS paths.** MathJax and mermaid.js are precompiled by
  `qjsc`; see the
  [evaluation](docs/eval/Markdown渲染qjsc预编译评估.md).
- **Generation filtering.** Every render bumps a request id; stale worker
  results are dropped before they touch the widget, and cancellation
  interrupts QuickJS.
- **One-time failure semantics.** A JS engine that fails to load stays failed
  until the view is reset, instead of retrying on every keystroke.

More detail: [architecture](docs/architecture.md),
[rendering pipeline](docs/rendering.md),
[diagnostics](docs/diagnostics.md).

## Requirements

- CMake 3.19 or newer
- Ninja
- A C++17 compiler
- Qt 5.14 or newer, or Qt 6.x, with Core, Gui, Widgets, Svg, Network,
  PrintSupport and Test modules

Validated configurations:

| Qt | Architecture | Status |
| --- | --- | --- |
| Qt 5.15.2 | x86 | Core, widget tests, `md_viewer`, install |
| Qt 5.14.2 | x64 | Core, widget tests, `md_viewer` |
| Qt 6.8.3 | x64 | Core, widget tests and `md_viewer` build; runtime visual check pending |
| Qt 6.8.3 | x64 (Linux) | Core and widget tests in CI; `md_viewer` disabled |
| Qt 5.12.12 | x64 | Not supported; Qt 5.14 is the minimum |

Windows with MSVC is the primary platform. Linux library/tests are covered by
CI on Ubuntu 24.04 with Qt 6.8.3; `md_viewer` is not built there yet. Ubuntu
20.04 ships Qt 5.12.8, which is below the minimum Qt 5.14; install a newer Qt
or Qt 6 to build on Linux.

## Build

Run these commands from a Visual Studio developer prompt on Windows.

On Linux, point `CMAKE_PREFIX_PATH` at Qt 5.14+ or Qt 6. The CI Linux job
disables `md_viewer` and runs the library tests with Qt 6.8.3.

```bash
cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_PREFIX_PATH=/path/to/Qt/5.15.2/msvc2019
cmake --build build
```

For Qt 6:

```bash
cmake -S . -B build-qt6 -G Ninja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DQLITEMD_QT_MAJOR=6 \
  -DCMAKE_PREFIX_PATH=/path/to/Qt/6.8.3/msvc2022_64
cmake --build build-qt6
```

Windows builds generate `build/bin/run_md_viewer.bat`, which sets `PATH`
and `QT_PLUGIN_PATH` before launching the viewer.

Build options:

- `QLITEMD_QT_MAJOR` — `5` (default) or `6`
- `QLITEMD_BUILD_QT_WIDGET` — build the Qt `markdown_view` component
  (default `ON`; set `OFF` for a core-only build)
- `QLITEMD_BUILD_MD_VIEWER` — build the example application (default `ON`
  at top level)
- `QLITEMD_BUILD_TESTS` — build the test suite (default `ON` at top level)
- `QLITEMD_INSTALL` — generate install rules (default `ON` at top level,
  `OFF` when embedded with `add_subdirectory`)
- `QLITEMD_SVG_BACKEND` — `resvg` or `qtsvg` (falls back to `qtsvg` when
  no `resvg` binary exists for the platform)

Formulas and Mermaid have no build-time backend switch: RaTeX/merman (when
their prebuilt libraries exist for the platform) and
MathJax/QuickJS/mermaid.js all compile in. The renderer picks `auto`, `native`
or `quickjs` at run time through `MdRenderOptions::formula_backend` and
`mermaid_backend` (C ABI: the same fields); `auto` tries the native backend
first and retries a failed formula or diagram with QuickJS.

The native backends are distributed as prebuilt DLLs under
`3rdparty/resvg/`, `3rdparty/merman/` and `3rdparty/ratex/`; a normal build
does not require Rust. Platforms without a native library fall back to the
QuickJS/QtSvg paths.

## Test

```bash
ctest --test-dir build --output-on-failure
```

## Install

```bash
cmake -S . -B build -DCMAKE_INSTALL_PREFIX=<prefix>
cmake --build build
cmake --install build
```

With the default options, the install contains `bin/md_viewer.exe`,
`bin/resvg.dll`, `bin/ratex_ffi.dll`, the `qlitemd_mdhtml` static library,
the `qlitemd_mdhtml_c` shared C ABI library, dependency archives (`md4c`,
tree-sitter, QuickJS), public core headers, license files and a CMake package
config. Where the platform has the prebuilt library, `bin/merman_ffi.dll` is
included too. On Windows, `windeployqt` is invoked during
install, and the in-tree `FluentUI3Style` and `ExWidgets` DLLs are copied next
to the viewer.

Consumers can then use:

```cmake
find_package(qlitemd CONFIG REQUIRED)
# C++ pipeline
target_link_libraries(myapp PRIVATE qlitemd::mdhtml)
# Shared C ABI (DLL/SO) for C#, Rust or C hosts
target_link_libraries(myapp PRIVATE qlitemd::mdhtml_c)
```

For a Qt-free SDK build (no Qt discovery, no viewer, no widget):

```bash
cmake -S . -B build-core -G Ninja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DQLITEMD_CORE_ONLY=ON
```

Install components:

- `Runtime` — `md_viewer`, FluentUI/ExWidgets DLLs and the deployed Qt runtime
- `Development` — core static/shared libraries, selected native backend DLLs, public core headers and `qlitemdConfig.cmake`
- `Licenses` — project and third-party license texts

```bash
cmake --install build --component Runtime
cmake --install build --component Development
cmake --install build --component Licenses
```

## Releases

Tags matching `v*` build a Qt 5.15.2 x64 Windows installer and a PDB archive
through GitHub Actions. The installer contains `md_viewer`, its Qt runtime and
the license files; the SDK remains a separate install component.

## Example application

`examples/md_viewer` is a complete FluentUI-style viewer for reading a
multi-level documentation set. It provides a recursive document tree with
filename filtering, file-level bookmarks, global full-text search with hit
navigation, session and per-document reading-position restore, a single-row
title bar that merges the menu, navigation, zoom and search, a right-hand
outline, segmented sidebar tabs with inline bookmark stars, focus mode,
Mica/acrylic window materials, a status bar with document count and build
backend labels, an opt-in prototype reading style (left-aligned page with
a 760px text measure, cards, transparent surface) with a breadcrumb, a
Ctrl+P quick-open palette for headings, recent files, documents and
commands, adjustable reader typography and measure, a welcome page with
recent documents and reading progress, a keyboard-accessible image
lightbox, and a large-file confirmation. The existing zoom, recent files,
image preview/save-as, formula and Mermaid source inspection, HTML/PDF
export and render-status panel remain. It is also the reference
integration for host-side policy such as link handling and settings.

## Layout

- `src/mdhtml/` — the zero-Qt core library (`qlitemd_mdhtml`)
- `src/markdown_view/` — the Qt widget component (`qlitemd::markdown_view`)
- `resources/` — theme CSS and JS payloads for the QuickJS Mermaid/formula paths
- `examples/md_viewer/` — the FluentUI example application
- `examples/qt_widget_minimal/` — add_subdirectory consumer smoke example
- `examples/minimal/` — zero-Qt console example for an installed SDK
- `examples/avalonia_mdhtml/` — .NET 10/Avalonia validation harness for the shared C ABI
- `tests/mdhtml/` — headless core GoogleTest suite
- `tests/markdown_view/` — headless widget GoogleTest suite
- `tests/md_viewer/` — headless tests for the example's tree/search/bookmark modules
- `cmake/` — package config template and test helpers
- `scripts/` — release packaging helpers (Qt source SDK)
- `installer/` — Inno Setup script for the Windows installer
- `licenses/` — license texts for JavaScript payloads
- `3rdparty/` — pinned, patched dependency snapshots
- `patches/` — provenance of the patches applied to those snapshots
- `docs/` — architecture, rendering and diagnostics notes
- `CHANGELOG.md` — release notes

## Known limitations

- Remote images are not downloaded; local files and `data:` URIs are
  supported.
- PDF export paginates by slicing the layout, so a very long element can be
  split across pages.
- Linux and Qt 6 have core/widget test coverage; `md_viewer` runtime and
  visual sign-off are still pending. A separate CI job builds the core with
  no Qt at all.
- Ubuntu 20.04's Qt 5.12.8 is below the minimum Qt 5.14; install a newer Qt.
- FluentUI uses the system `Segoe Fluent Icons` font and falls back to
  `Segoe MDL2 Assets`; the Microsoft font is not redistributed with this
  repository. A machine with neither font shows placeholder boxes.

## Third-party libraries

qlitemd would not exist without these projects. Thank you to their
maintainers and contributors.

| Project | Version | Used for | License |
| --- | --- | --- | --- |
| [litehtml](https://github.com/litehtml/litehtml) | 0.10 (snapshot) | HTML/CSS layout | BSD-3-Clause |
| [qlitehtml](https://github.com/qtproject/qlitehtml) | 1.0 (snapshot) | Qt widget bridge | BSD-3-Clause |
| [md4c](https://github.com/mity/md4c) | 0.6.0 | Markdown parser | MIT |
| [quickjs-ng](https://github.com/quickjs-ng/quickjs) | 0.17.0 | JavaScript runtime (MathJax and mermaid.js fallback) | MIT |
| [RaTeX](https://github.com/erweixin/RaTeX) | 0.1.15 | Native TeX to SVG | MIT |
| [gumbo-parser](https://github.com/google/gumbo-parser) | bundled with litehtml | HTML5 parser | Apache-2.0 |
| [tree-sitter](https://github.com/tree-sitter/tree-sitter) | 0.27.1 | Incremental parsing core for syntax highlighting | MIT |
| [tree-sitter grammars](https://github.com/tree-sitter/tree-sitter) | 0.23.x-0.25.x | Language grammars (12 languages) | MIT |
| [MathJax](https://www.mathjax.org/) | 3.2.1 (mathjax-full) | TeX to SVG | Apache-2.0 |
| [merman](https://github.com/Latias94/merman) | 0.9.2 | Native Mermaid rendering | MIT OR Apache-2.0 |
| [resvg](https://github.com/linebender/resvg) | 0.48.1 | SVG rasterization | Apache-2.0 OR MIT |
| [Mermaid](https://mermaid.js.org/) | 11.16.0 | Legacy diagram backend | MIT |
| [mermaidx](https://github.com/MohammadRaziei/mermaidx) | 0.9.5 | Legacy payload extraction / DOM shim | MIT |
| [QWindowKit](https://github.com/stdware/qwindowkit) | 1.5.0 | Frameless windows (`md_viewer`) | Apache-2.0 |
| [qmsetup](https://github.com/stdware/qmsetup) | 1.0.0 (4a3ff82) | CMake helpers for QWindowKit | MIT |
| [FluentUIStyle](https://github.com/XHY-ChuJian/FluentUIStyle) | 4.0 | Fluent UI shell (`md_viewer`) | MIT |
| [spdlog](https://github.com/gabime/spdlog) | 1.17.0 | Logging (`md_viewer`) | MIT |
| [GoogleTest](https://github.com/google/googletest) | 1.17.0 | Tests | BSD-3-Clause |
| [Qt](https://www.qt.io/) | 5.14+ / 6.x | Application framework (external) | LGPLv3 / GPLv3 / commercial |

Versions are the snapshots vendored in this repository; upstream projects may
have newer releases. Full license texts are in [NOTICE](NOTICE) and, after
installation, under `licenses/`.

## Development note

The code in this repository is largely AI-generated. The maintainer provided
the design goals, constraints and review; an AI coding assistant produced and
iterated on the implementation. Commit history does not add AI co-author
trailers. The maintainer is responsible for the final decisions and validation.

## License

qlitemd's own code is MIT. Third-party components keep their licenses; see
[NOTICE](NOTICE).
