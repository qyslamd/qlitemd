# Rendering

## Markdown to HTML

md4c parses the source; raw HTML is preserved. Footnotes (`[^1]`) are
enabled. Admonitions are selected per render:

- `MdAdmonitionMode::kGithub` uses md4c's built-in admonitions. The marker
  must be alone on its blockquote line, the title is the type name, and only
  note/tip/important/warning/caution are recognized.
- `MdAdmonitionMode::kMpe` (default) rewrites blockquotes into MPE-style
  callouts. Text after the marker on the same line is the title; a marker
  alone uses a default title. It supports note/info/tip/important/warning/
  caution/danger/success/question. Collapsible `[!note]+/-` is not supported.
  Each title carries an explicit `.callout-icon` span with a Unicode glyph,
  so hosts without CSS generated content still show the symbol.

The core extracts formula segments, collects GFM code blocks, emits code
frames with copy links and optional line numbers, and adds heading ids plus
`data-md-slug` anchors.

## Formula and Mermaid

The core only produces SVG. TeX uses native RaTeX on Windows and MathJax in
QuickJS elsewhere. Mermaid defaults to mermaid.js in QuickJS because it has
broader syntax coverage than merman-ffi; select
`QLITEMD_MERMAID_BACKEND=merman` for the native backend (it requires resvg).
The QuickJS payloads are build-time bytecode, so no Node.js is needed at
runtime. Native backends are prebuilt DLLs under `3rdparty/resvg/`,
`3rdparty/merman/` and `3rdparty/ratex/`.

HTML references each asset as `<img src="qlitemd-asset:<id>" width=...
height=...>`; `<id>` is the first 128 bits of SHA-256 over the asset kind and
render key. The host resolves it through `MdHtmlResult::assets`.

`md_viewer` rasterizes each SVG with resvg (or QtSvg), rewrites the URL to the
historical `mj:f/<hash>` / `mj:m/<hash>` token, and serves the pixmap through
the qlitehtml callback. Its persistent PNG cache stores SVGs rasterized by the
previous build under the same keys.

## Asynchronous commit

Each render job has a coordinator thread: it parses, submits tasks to the
bounded worker lanes, waits for results and assembles in source order, so
worker completion order never affects the HTML. Progress callbacks run on the
coordinator. Cancellation drops queued tasks and interrupts QuickJS; native
Mermaid/RaTeX work finishes the current diagram.

The Qt adapter waits on its own thread, rasterizes SVGs off the GUI thread and
posts the finished result back with a queued invocation. A later generation
always wins: stale results are discarded, and cancellation rolls the view back
to the last committed document.
