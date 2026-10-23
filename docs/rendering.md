# Rendering

## Markdown to HTML

md4c parses the source; raw HTML is preserved. Formula and Mermaid blocks
are replaced with placeholder tokens before the HTML is handed to
qlitehtml.

## Formula and Mermaid

On Windows, TeX is rendered by native RaTeX and Mermaid by native
merman-ffi; no JavaScript engine is built or shipped. Other platforms keep
the QuickJS fallback: MathJax for formulas and mermaid.js for diagrams
(`QLITEMD_MERMAID_BACKEND=quickjs`). Both paths produce SVG, which is
rasterized by resvg (or QtSvg with `QLITEMD_SVG_BACKEND=qtsvg`) and inserted
into the HTML as an image token (`mj:f/<hash>` or `mj:m/<hash>`). Tokens are
served by the MarkdownView resource callback and cached in the render
context.

On non-Windows builds MathJax and mermaid.js are build-time QuickJS
bytecode payloads; no Node.js is needed at runtime. Native backends are
shipped as prebuilt DLLs under `3rdparty/resvg/`, `3rdparty/merman/` and
`3rdparty/ratex/`.

## Asynchronous commit

The GUI thread parses and collects render tasks. Worker pools produce
highlighted code, formula pixmaps and Mermaid pixmaps. A later generation
always wins: stale results are discarded, and cancellation rolls the view
back to the last committed document.
