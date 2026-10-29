# Embedding the Qt widget

`qlitemd::markdown_view` is a Qt Widgets component. It is consumed from
source so the host application owns the Qt version, compiler and
architecture. The installed SDK is only the zero-Qt `mdhtml` core.

## add_subdirectory

```cmake
set(QLITEMD_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(QLITEMD_BUILD_MD_VIEWER OFF CACHE BOOL "" FORCE)
set(QLITEMD_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(path/to/qlitemd)

target_link_libraries(myapp PRIVATE qlitemd::markdown_view)
```

The consumer's Qt kit must be discoverable through `CMAKE_PREFIX_PATH` or
`Qt5_DIR`/`Qt6_DIR`. qlitemd infers the major version from an already
created `Qt6::Core`/`Qt5::Core` target; otherwise pass
`-DQLITEMD_QT_MAJOR=6` (or `5`).

```cpp
auto* view = new MarkdownView(parent);
view->LoadFile(path);
```

`MarkdownView` provides rendering, Mermaid/formula images, code copy links,
search, zoom, outline data, HTML/PDF export, printing and theme switching.
The document tree, bookmarks and settings window in `md_viewer` are
application shell code and are not part of the widget.

## Options

| Option | Default | Meaning |
| --- | --- | --- |
| `QLITEMD_BUILD_QT_WIDGET` | `ON` | Build `qlitemd_markdown_view` |
| `QLITEMD_BUILD_MD_VIEWER` | top-level `ON` | Build the demo application |
| `QLITEMD_BUILD_TESTS` | top-level `ON` | Build the GoogleTest suites |
| `QLITEMD_INSTALL` | top-level `ON` | Generate install rules |
| `QLITEMD_QT_MAJOR` | inferred or `5` | Qt major version |
| `QLITEMD_SVG_BACKEND` | `resvg` | `resvg` or `qtsvg` |

Formulas and Mermaid have no build-time backend switch. RaTeX/merman (when
the platform has their prebuilt libraries) and
MathJax/QuickJS/mermaid.js are all compiled in; use
`MarkdownView::SetFormulaBackend()` and `SetMermaidBackend()` to choose
auto/native/QuickJS at run time.

A selected native backend's DLL/SO must be next to the host executable at
runtime, or found through `PATH`/`LD_LIBRARY_PATH`. qlitemd stages the
selected libraries under its own `bin/` directory during the build; the
host packager is responsible for the final application layout. With
`qtsvg` and no native formula/Mermaid libraries, no native backend DLL is
required.

## Source package

Release CI produces `qlitemd-<version>-qt-src.zip`. It contains the core
and widget sources, themes/JS payloads, the 12 tree-sitter grammars, md4c,
qlitehtml/litehtml, QuickJS, and the Windows/Linux native backend binaries.
It does not contain the `md_viewer` application shell or the optional
merman backend. `examples/qt_widget_minimal` inside the package is the
reference consumer and CI smoke test.

## Constraints

- qlitemd defines the vendored targets `md4c`, `qlitehtml`, `litehtml`,
  `qjs`, `qjsc`, `tree_sitter_core` and `tree_sitter_grammars`. A host that
  already defines the same target names should consume the package in an
  isolated subproject.
- The widget is a static library in this source-package model; every host
  links its own copy. A shared widget DLL is a separate packaging decision.
- `MarkdownView` headers and the core headers are intentionally
  namespace-free, matching the rest of the project.
