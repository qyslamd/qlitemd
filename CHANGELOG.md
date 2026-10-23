# Changelog

All notable changes to qlitemd are documented here.

## Unreleased

- Change the opt-in prototype reading layout from a centered 700 px
  column to a left-aligned page: headings, body text, lists and quotes
  cap at 760 px, while Mermaid diagrams, code blocks and tables use the
  full reader width. Wide sequence diagrams were previously scaled down
  to about 620 px and became unreadable; 2x sampling could not help.
- md_viewer now targets documentation-set reading: a recursive tree that
  only shows directories containing `.md`/`.markdown`, a filename/path
  filter, file-level bookmarks with a Ctrl+D star, and global full-text
  search with streamed, cancellable results and approximate hit navigation
  through `FindText`.
- Restore the last root, document and per-document scroll position on
  startup (500-entry LRU), confirm documents larger than 20 MiB once per
  session, and rebuild the tree through `QFileSystemWatcher` (300 ms
  debounce, root-only beyond 200 directories) or F5.
- Merge the menu, navigation, zoom and global search into a single-row
  FluentUI title bar. The vendored FluentUI gains leading/trailing widget
  hooks (recorded as `patches/FluentUIStyle/0003-titlebar-widgets.patch`).
  Window material is selectable between auto (Mica on Windows 11, DWM blur
  otherwise), acrylic and off; the markdown body keeps the theme surface.
- Fix formula image clipping when the source SVG uses `pt`/`ex` units:
  resvg now scales by the target/intrinsic ratio instead of the raw
  supersampling factor. Render-cache pipeline bumped to v3 so old clipped
  entries miss; `SvgRasterizerTest.PtUnitsFillTargetWithoutClipping` pins it.
- Add `MarkdownView::SetThemeStyleSheet()` and `SetPageBackground()`:
  hosts can inject per-theme CSS and choose an opaque or transparent page
  fill. `md_viewer` gains an opt-in prototype reading style (cards,
  transparent surface) with a breadcrumb; the library default style and
  the existing `MD_MARKDOWN_CSS` tweak path remain unchanged.
- Align `md_viewer` with the UI prototype: outline moved to a right-hand
  column, segmented sidebar tabs with icons, filter above the tabs, a
  single-column document tree with folder/file icons, per-folder counts,
  file sizes and inline stars, two-line bookmark cards, card-style search
  hits with a yellow match mark, a ready/count/backend status bar, focus
  mode (Ctrl+Shift+F) and a window-material menu. The title bar shows the
  root-relative breadcrumb. The tree keeps file sizes (the prototype used
  only counts); reading-area typography remains the library theme.
- Fix dark-theme switching in `md_viewer`: recreate the FluentUI style and
  push its standard palette to the application, then explicitly refresh the
  status bar, sidebar, outline and reader palettes. The reader now paints the
  theme page background (`#ffffff` / `#0d1117`), so short documents no longer
  expose a light strip below the rendered page in either theme.
- Rework the `md_viewer` status bar to follow the prototype: a green ready
  dot and the document's native full path on the left, with document count,
  build backends and zoom grouped on the right. The prototype's “indexed”
  stat is not shown because the viewer searches on demand and keeps no
  persistent index.
- Add headless `tests/md_viewer` coverage for navigation history, document
  indexing, bookmarks/reading positions, text search, the sidebar panel and
  the title-bar hooks.

## 0.0.2 - 2026-10-23

- Formula and Mermaid backends are compile-time choices on every
  platform: `QLITEMD_FORMULA_BACKEND=ratex|quickjs` and
  `QLITEMD_MERMAID_BACKEND=merman|quickjs`. Missing native binaries fall
  back to quickjs/qtsvg automatically.
- Ship Linux x64 builds of the native backends
  (`3rdparty/*/bin/linux-x64/lib*.so`), built from the same sources as
  the Windows DLLs; verified on Qt 5.12.8 in WSL with both backend sets.
- Windows: render formulas with native RaTeX instead of MathJax in
  QuickJS; Windows builds now ship no JavaScript engine at all. Other
  platforms keep the quickjs-ng fallback.
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
