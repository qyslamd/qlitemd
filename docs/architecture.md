# Architecture

qlitemd has three layers:

1. `src/mdhtml/` is the zero-Qt core (`qlitemd_mdhtml`). `MdHtmlPipeline`
   parses Markdown, collects render tasks, waits on three worker lanes
   (highlight, formula, Mermaid) and assembles deterministic HTML plus SVG
   assets. Font metrics are injectable; the default implementation is a
   deterministic approximation.
2. `src/mdhtml/mdhtml_c.*` is the shared C ABI (`qlitemd_mdhtml_c`). It
   exposes versioned, `__cdecl` entry points, synchronous and asynchronous
   jobs with progress/cancel, and accessors for HTML, assets, outline and
   code blocks. `struct_size` fields version the result structs and
   `MDHTML_ABI_VERSION` gates incompatible layouts. The layer is C11-safe
   and owns every string it returns. The shared target compiles only the C
   ABI translation unit; it embeds the static core and its static
   dependencies. On ELF platforms those targets are built with
   `POSITION_INDEPENDENT_CODE` so the shared link succeeds.
3. `src/markdown_view/` is the Qt widget layer. `MarkdownView` embeds
   qlitehtml in a QWidget, turns core SVG assets into QPixmaps with
   resvg/QtSvg, keeps the persistent PNG cache, and owns navigation,
   selection, zoom, export and image lookup.

The installed SDK contains layers 1 and 2 (`find_package(qlitemd)` →
`qlitemd::mdhtml`, `qlitemd::mdhtml_c`). The Qt widget is target
`qlitemd::markdown_view` and is consumed from source with
`add_subdirectory`; release CI ships it as `qlitemd-<version>-qt-src.zip`
alongside the compiled core SDK. qlitehtml, QuickJS, merman, resvg and md4c
types stay out of the public core headers so the backend can be replaced
without changing callers. Mermaid defaults to mermaid.js in QuickJS for
broader syntax coverage; native merman-ffi remains selectable. Formulas use
RaTeX on Windows and MathJax in QuickJS elsewhere. `md_viewer` rasterizes
with resvg by default, with the legacy QtSvg path still selectable.

`examples/avalonia_mdhtml` validates layer 2 from .NET: it P/Invokes the
async job API, rasterizes SVG assets with Svg.Skia and displays the HTML with
LiteHtmlSharp.Avalonia. It is a harness, not a shipped viewer.

Dependencies are pinned under `3rdparty/`; patches are recorded under
`patches/`.
