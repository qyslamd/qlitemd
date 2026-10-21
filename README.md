# qlitemd

**A Qt Widgets Markdown renderer that doesn't need a browser engine.**

md4c parses the document, qlitehtml lays it out, and QuickJS runs
highlight.js, MathJax and Mermaid. Formula and diagram SVGs are rasterized
with QPainter, so there is no WebEngine, Chromium or Node.js at runtime.

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
[![Qt](https://img.shields.io/badge/Qt-5.14%2B-green.svg)](#requirements)
[![C++17](https://img.shields.io/badge/C%2B%2B-17-blue.svg)](#requirements)

[中文](README.zh-CN.md) · [Example](examples/md_viewer) · [Docs](docs/)

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
target_link_libraries(myapp PRIVATE qlitemd_markdown)
```

Or, from an installed SDK:

```cmake
find_package(qlitemd CONFIG REQUIRED)
target_link_libraries(myapp PRIVATE qlitemd::markdown)
```

```cpp
#include "markdown_view.h"

auto* view = new MarkdownView(parent);
view->LoadFile(QStringLiteral("README.md"));
// or: view->SetContent(markdown_text, base_directory);
```

## Features

- **GFM Markdown** — tables, task lists, strikethrough and autolinks through
  md4c; raw HTML is preserved.
- **Syntax highlighting** — highlight.js runs in QuickJS; the example renders
  per-line spans so line numbers stay aligned.
- **Formulas** — MathJax (TeX to SVG to QPixmap), inline `$...$` and display
  `$$...$$`, with theme-aware colors.
- **Mermaid diagrams** — the official Mermaid bundle runs in QuickJS with a
  small DOM shim; SVG is cleaned and rasterized at 2x.
- **Themes** — built-in light and dark CSS; `MD_MARKDOWN_CSS` can override the
  stylesheet for live tweaking.
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
      |-- highlight.js  ->  highlighted HTML
      |-- MathJax       ->  SVG  ->  QPainter  ->  PNG
      `-- Mermaid       ->  SVG  ->  QPainter  ->  PNG
      |
      v
assemble HTML  ->  qlitehtml widget  ->  painted QWidget
```

Key design choices:

- **Dependency inversion.** The public API knows nothing about QuickJS.
  Highlighting, formula and Mermaid work are passed in as plain callbacks, so
  tests can use fakes and hosts can swap engines.
- **Bytecode, not source.** Five JS payloads are precompiled by `qjsc` at
  build time. This cuts a first render with formulas and diagrams from about
  1.57 s to 0.67 s; see the
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
| Qt 5.15.2 | x86 | Library, tests, `md_viewer`, install |
| Qt 5.14.2 | x64 | Library, tests, `md_viewer` |
| Qt 6.8.3 | x64 | Library, tests and `md_viewer` build; runtime visual check pending |
| Qt 5.12.12 | x64 | Not supported; Qt 5.14 is the minimum |

Windows with MSVC is the primary platform. Linux has not been validated.

## Build

Run these commands from a Visual Studio developer prompt on Windows.

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

Build options:

- `QLITEMD_QT_MAJOR` — `5` (default) or `6`
- `QLITEMD_BUILD_MD_VIEWER` — build the example application (default `ON`)
- `QLITEMD_BUILD_TESTS` — build the test suite (default `ON`)

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

With the default options, the install contains `bin/md_viewer.exe`, the
`qlitemd_markdown` static library, its static dependency archives, public
headers, license files and a CMake package config. On Windows, `windeployqt`
is invoked during install, and the in-tree `FluentUI3Style` and `ExWidgets`
DLLs are copied next to the viewer.

Consumers can then use:

```cmake
find_package(qlitemd CONFIG REQUIRED)
target_link_libraries(myapp PRIVATE qlitemd::markdown)
```

For a library-only SDK, configure with `QLITEMD_BUILD_MD_VIEWER=OFF` and
`QLITEMD_BUILD_TESTS=OFF`.

## Example application

`examples/md_viewer` is a complete FluentUI-style viewer built on the
library. It provides single-instance behavior, a directory sidebar, an
outline, find, zoom, recent files, image preview/save-as, formula and
Mermaid source inspection, HTML/PDF export and a render-status panel. It is
also the reference integration for host-side policy such as link handling
and settings.

## Layout

- `src/markdown/` — the library
- `resources/` — theme CSS and JS payloads
- `examples/md_viewer/` — the example application
- `tests/markdown/` — headless GoogleTest suite
- `cmake/` — package config template and test helpers
- `3rdparty/` — pinned, patched dependency snapshots
- `patches/` — provenance of the patches applied to those snapshots
- `docs/` — architecture, rendering and diagnostics notes

## Known limitations

- Remote images are not downloaded; local files and `data:` URIs are
  supported.
- PDF export paginates by slicing the layout, so a very long element can be
  split across pages.
- Qt 6 and Linux have build coverage but no visual/runtime sign-off yet.
- FluentUI falls back to a system icon font because the Microsoft Segoe
  Fluent Icons font is not redistributed with this repository.

## Acknowledgements

This project builds on [qlitehtml](https://github.com/qtproject/qlitehtml),
[litehtml](https://github.com/litehtml/litehtml),
[md4c](https://github.com/mity/md4c), [QuickJS](https://bellard.org/quickjs/),
[highlight.js](https://highlightjs.org/), [MathJax](https://www.mathjax.org/),
[Mermaid](https://mermaid.js.org/), [spdlog](https://github.com/gabime/spdlog),
[GoogleTest](https://github.com/google/googletest), QWindowKit, qmsetup and
FluentUIStyle. Qt is an external dependency and is not bundled.

## License

qlitemd's own code is MIT. Third-party components keep their licenses; see
[NOTICE](NOTICE).
