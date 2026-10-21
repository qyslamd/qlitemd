# Rendering

## Markdown to HTML

md4c parses the source; raw HTML is preserved. Formula and Mermaid blocks
are replaced with placeholder tokens before the HTML is handed to
qlitehtml.

## Formula and Mermaid

QuickJS executes the pinned MathJax and Mermaid payloads. The resulting
SVG is converted to a QPixmap and inserted into the HTML as an image
token (`mj:f/<hash>` or `mj:m/<hash>`). Tokens are served by the
MarkdownView resource callback and cached in the render context.

MathJax, Mermaid and highlight.js are build-time payloads; the runtime
does not need Node.js.

## Asynchronous commit

The GUI thread parses and collects render tasks. Worker pools produce
highlighted code, formula pixmaps and Mermaid pixmaps. A later generation
always wins: stale results are discarded, and cancellation rolls the view
back to the last committed document.
