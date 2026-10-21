# Changelog

All notable changes to qlitemd are documented here.

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
