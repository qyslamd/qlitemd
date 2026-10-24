# qlitemd minimal consumer

A zero-Qt console program built against the installed qlitemd SDK. It renders
Markdown through `MdHtmlPipeline`, prints the body HTML, and optionally writes
every formula/Mermaid SVG to a directory:

```bash
cmake -S . -B build -DCMAKE_PREFIX_PATH=<qlitemd install prefix>
cmake --build build
./build/qlitemd_minimal document.md --assets out-assets
```

The program links only `qlitemd::mdhtml`; no Qt, qlitehtml or rasterizer is
involved.
