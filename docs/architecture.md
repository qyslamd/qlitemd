# Architecture

qlitemd has two layers:

1. `src/mdhtml/` is the zero-Qt core (`qlitemd_mdhtml`). `MdHtmlPipeline`
   parses Markdown, collects render tasks, waits on three worker lanes
   (highlight, formula, Mermaid) and assembles deterministic HTML plus SVG
   assets. Font metrics are injectable; the default implementation is a
   deterministic approximation. A synchronous C API lives in `mdhtml_c.h`.
2. `examples/md_viewer/qt_adapter/` is the Qt adapter. `MarkdownView` embeds
   qlitehtml in a QWidget, turns core SVG assets into QPixmaps with
   resvg/QtSvg, keeps the persistent PNG cache, and owns navigation,
   selection, zoom, export and image lookup.

The installed SDK is the core only (`find_package(qlitemd)` →
`qlitemd::mdhtml`). The adapter is compiled into `md_viewer` and is not
installed. qlitehtml, QuickJS, merman, resvg and md4c types stay out of the
public core headers so the backend can be replaced without changing callers.
Mermaid uses native merman-ffi by default and formulas use RaTeX on Windows;
the QuickJS fallback covers other platforms. `md_viewer` rasterizes with
resvg by default, with the legacy QtSvg/QuickJS path still selectable.

Dependencies are pinned under `3rdparty/`; patches are recorded under
`patches/`.
