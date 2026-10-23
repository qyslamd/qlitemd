# Architecture

`qlitemd` has three layers:

1. `MarkdownToHtml` converts Markdown to HTML with md4c and extracts
   formula/Mermaid placeholders.
2. `MarkdownRenderService` runs code highlighting, formula rendering and
   Mermaid rendering in worker pools. The GUI thread parses, collects
   tasks, assembles the final HTML and commits it atomically.
3. `MarkdownView` embeds qlitehtml in a QWidget and owns navigation,
   selection, zoom, export, image lookup and the render cache.

The public entry point is `MarkdownView`. qlitehtml, QuickJS, merman,
resvg and md4c types stay out of its header so the rendering backend can
be replaced without changing callers. Mermaid uses native merman-ffi by
default; SVG rasterization uses resvg. Both native dependencies are
prebuilt DLLs; the legacy QtSvg/QuickJS Mermaid path remains selectable.

Dependencies are pinned under `3rdparty/`; patches that make them work
with Qt 5/6 are recorded under `patches/`.
