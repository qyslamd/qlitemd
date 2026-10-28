# qlitemd Qt widget minimal

This is the smallest consumer of the `qlitemd::markdown_view` source
component. It is also the `add_subdirectory` smoke test used by CI.

```powershell
cmake -S . -B build -G Ninja `
  -DCMAKE_PREFIX_PATH=D:/Qt22/5.15.2/msvc2019
cmake --build build --parallel 2
.\build\qlitemd_qt_widget_minimal.exe --screenshot shot.png
```

The consumer owns the Qt kit. `qlitemd` is added as a subdirectory with its
demo app, tests and install rules disabled; only the core and the Qt widget
target are built.

For a Qt 6 kit, add `-DQLITEMD_QT_MAJOR=6`.
