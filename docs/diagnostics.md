# Diagnostics

`MarkdownView` exposes renderer and cache counters through
`renderer_status()`, `cache_usage()`, `last_render_timings()` and
`render_history()`.

`MarkdownDiagnostics` has two optional switches:

- `trace`: emit stage-level timing messages.
- `dump_directory`: write intermediate HTML and payload artifacts.

Both are off by default. The example `md_viewer` displays the counters in
its render-status dock; the dump directory must be a writable path owned
by the caller.
