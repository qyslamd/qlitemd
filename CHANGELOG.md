# Changelog

All notable changes to qlitemd are documented here.

## Unreleased

- Replace the base64 `<img>` pipeline with a `RenderedImage` struct:
  workers hand the view a decoded bitmap plus original PNG bytes, the
  view assembles `mj:`-token tags directly (no base64/PNG decode on the
  GUI thread), and the persistent cache stores binary entries (pipeline
  v2; v1 text entries miss once and are pruned).
- Remove the synchronous `FormulaToImgTag`/`MermaidToImgTag` facade and
  the global `ClearFormulaCache`/`ClearMermaidCache`/`*RendererReady`
  helpers; embedders render through `MarkdownView`.
- Add a native Mermaid backend based on merman-ffi; the legacy
  QuickJS + mermaid.js path remains selectable with
  `QLITEMD_MERMAID_BACKEND=quickjs`.
- Rasterize formula and Mermaid SVG with resvg by default; QtSvg remains
  selectable with `QLITEMD_SVG_BACKEND=qtsvg`.
- Ship prebuilt `merman_ffi.dll` and `resvg.dll` for Windows x86/x64;
  normal builds do not require Rust.
- Add `docs/guide/merman构建指南.md` and integrate both native DLLs into
  the SDK and installer components.
- Add a class-diagram render test and runtime DLL staging for tests and
  the example application.
- Generate `run_md_viewer.bat`/`.sh` with Qt `PATH` and `QT_PLUGIN_PATH`
  prepared before launch.
- Fix FluentUI glyph fallback when Segoe Fluent Icons is unavailable;
  `md_viewer` now uses the system font (or Segoe MDL2 Assets) instead of
  drawing placeholder boxes.

## 0.0.1 - 2026-10-21

Initial public release.

- `MarkdownView` widget built on qlitehtml and md4c.
- Asynchronous syntax highlighting, MathJax formulas and Mermaid diagrams
  executed in QuickJS.
- Light and dark themes, outline, find, zoom and persistent render cache.
- HTML and PDF export, image preview/save-as and formula/Mermaid source
  inspection in `md_viewer`.
- Qt 5.14+ and Qt 6 support; Windows MSVC and Linux CI coverage.
- CMake package config with `qlitemd::markdown`.
