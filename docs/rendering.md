# Rendering

## Markdown to HTML

md4c parses the source; raw HTML is preserved. Formula and Mermaid blocks
are replaced with placeholder tokens before the HTML is handed to
qlitehtml.

## Formula and Mermaid

MathJax runs in QuickJS. Mermaid uses the native merman-ffi backend by
default; the legacy QuickJS + mermaid.js backend remains selectable with
`QLITEMD_MERMAID_BACKEND=quickjs`. Both produce SVG, which is rasterized by
resvg (or QtSvg with `QLITEMD_SVG_BACKEND=qtsvg`) and inserted into the HTML
as an image token (`mj:f/<hash>` or `mj:m/<hash>`). Tokens are served by the
MarkdownView resource callback and cached in the render context.

MathJax, highlight.js and the optional legacy Mermaid path are build-time
QuickJS payloads; the runtime does not need Node.js. The native backends are
shipped as prebuilt DLLs under `3rdparty/resvg/` and `3rdparty/merman/`.

## Asynchronous commit

The GUI thread parses and collects render tasks. Worker pools produce
highlighted code, formula pixmaps and Mermaid pixmaps. A later generation
always wins: stale results are discarded, and cancellation rolls the view
back to the last committed document.
