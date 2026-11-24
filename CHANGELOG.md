# Changelog

All notable, user-visible changes are documented here. Releases before
0.5.0 are kept to one line each; see git history or that tag's GitHub
Release for details.

## Unreleased

### Added

- A 30 px activity bar on the left replaces the dock layout: documents,
  workspace search, bookmarks and render status share one side panel;
  clicking the active icon collapses or reopens it. View, panel and
  outline visibility are persisted (`sidebar_view`, `sidebar_visible`,
  `outline_visible`); `Ctrl+Shift+E/F/B/R` switch views and
  `Ctrl+Shift+O` toggles the outline.
- The side panel, content and outline live in a `QSplitter`; the
  activity bar stays fixed while the sidebar and outline widths can be
  dragged and are persisted (`sidebar_width`, `outline_width`).

### Changed

- The settings gear moved to the bottom of the activity bar; the document
  bar and welcome bar no longer carry their own copies.
- Search progress, hit count and the cancel button now sit in the line
  under the search box. The sidebar footer is gone, so the document tree
  and the page share the same bottom edge.

### Removed

- `QDockWidget`-based layout, its saved state and the dock entries in the
  View menu.

## 0.7.0 - 2026-11-24

### Added

- The document bar became a real `QToolBar` across the top of the window,
  above both docks, with a dedicated welcome bar (icon, workspace name,
  quick open).
- Search is split by scope: `Ctrl+F` finds within the current document
  (incremental, Enter/Shift+Enter, wrap hints); workspace search lives in
  the sidebar Search view behind `Ctrl+Shift+F` and starts disabled
  without a workspace.

### Changed

- Tabs are 38 px tall with a 4 px gap above them; the title bar grew to
  42 px. The trailing "+" button was removed.
- The window can shrink to 600 px (dock and welcome-card minimum widths
  reduced).
- The settings glyph became a gear; the welcome tab uses the colored app
  icon.

### Fixed

- The reader-appearance popup was constructed again after the P5 shell
  rework had left the button without a target.
- The very first welcome screen showed the document bar instead of the
  welcome bar.

## 0.6.0 - 2026-11-24

### Added

- Multi-tab viewer: every tab owns its history, scroll position, zoom,
  outline and bookmark state; the welcome tab is permanent at index 0;
  tabs reorder by dragging and `Ctrl+Tab` cycles them; the session stores
  `open_tabs + active`.
- Opening conventions: a plain click replaces the current tab,
  `Ctrl+click` or the middle button opens a new one; files outside the
  workspace no longer re-root it.
- The render-settings dock became a modeless dialog with a category list,
  stacked pages, cards and immediate apply-to-all-tabs.

### Changed

- The shell follows the three-tier layout: title bar with menu, tabs and
  window buttons; 32 px document bar; 26 px status bar; the path appears
  once.
- Tab appearance and metrics: 220x30 preferred / 36x26 minimum, rounded
  corners that flare on selection, hover overlay, loading spinner,
  hidden close buttons on narrow tabs, reload and reopen-closed-tab
  context actions.

### Fixed

- Closing a tab while its panels were still attached, session restore
  overwriting the remembered tabs, and secondary windows persisting the
  shared session.

## 0.5.0 - 2026-11-23

### Added

- File menu and quick-open gained "卸载工作区": it saves the reading
  position, clears the workspace and returns to the welcome page; recent
  files, bookmarks and positions survive and the next start does not
  restore the old workspace.

### Changed

- Theme-dependent SVG icons were split into light and dark sets;
  semantic icons stay single-set.
- `MarkdownView::ClearDocument()` and `NavigationHistory::Clear()` are
  public for hosts that unload a document.

## 0.4.0 - 2026-11-20

Syntax highlighting returned to highlight.js in QuickJS, dropping the
vendored tree-sitter core and 12 grammars; binaries roughly halved.

## 0.3.1 - 2026-11-20

Focus mode removed; the executable embeds its application icon.

## 0.3.0 - 2026-11-19

The viewer moved to `apps/qt_md_viewer` with multi-window support, its own
qrc icon set and the bundled MiSans font; fixed inverted caption-button
icons and the menu chevron overlap.

## 0.2.1 - 2026-10-29

The Qt source package ships merman so `add_subdirectory` consumers keep the
full Mermaid engine selection.

## 0.2.0 - 2026-10-29

Formula and Mermaid engines switch at runtime (native RaTeX/merman first,
QuickJS fallback) through `MdRenderOptions`.

## 0.1.0 - 2026-10-28

`add_subdirectory`-friendly widget layer, footnotes and selectable
admonition modes, a shared C ABI and the Avalonia validation harness.

## 0.0.3 - 2026-10-24

Reading experience: quick-open palette, reader-appearance popup, welcome
page, image lightbox, document-set tree, bookmarks and full-text search.

## 0.0.2 - 2026-10-23

Native prebuilt RaTeX/merman/resvg backends and the `RenderedImage`
pipeline (no base64 on the GUI thread).

## 0.0.1 - 2026-10-21

First public release: `MarkdownView`, QuickJS highlighting/formulas/Mermaid,
light and dark themes, outline, find, zoom, render cache, HTML/PDF export.
