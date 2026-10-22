# Changelog

All notable changes to qlitemd are documented here.

## Unreleased

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
