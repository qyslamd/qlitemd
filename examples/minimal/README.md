# Minimal example

A standalone `QWidget` + `MarkdownView` example. It does not use FluentUI,
QWindowKit or the larger `md_viewer` application, so it is the smallest
integration test for an installed qlitemd SDK.

```bash
cmake -S examples/minimal -B build-minimal -G Ninja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_PREFIX_PATH="<qlitemd-prefix>;<Qt-prefix>"
cmake --build build-minimal
./build-minimal/qlitemd_minimal
```

On Linux without a display:

```bash
QT_QPA_PLATFORM=offscreen ./build-minimal/qlitemd_minimal
```
