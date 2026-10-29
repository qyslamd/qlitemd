# qlitemd mdhtml — Avalonia validation app

This is a validation harness, not a product viewer. It proves that the
zero-Qt `qlitemd_mdhtml` core can be consumed from a non-C++ GUI:

- C# P/Invoke into the shared C ABI (`qlitemd_mdhtml_c.dll`);
- asynchronous render job + progress;
- HTML + SVG assets + code-block/copy-link metadata;
- HTML display through `LiteHtmlSharp.Avalonia` (litehtml);
- SVG assets rasterized by `Svg.Skia` before being handed to litehtml.

It targets **.NET 10 / Avalonia 11.3.22**. `LiteHtmlSharp.Avalonia` 2.0.7 is
built against Avalonia 11.3.9; the 11.3.x line is intentional.

## Build the native core (x64)

The Avalonia app is x64, so the C DLL must be x64 even if the normal
developer build is Qt x86. From a Visual Studio x64 developer prompt:

```powershell
cmake -S . -B build_core_x64 -G Ninja `
  -DCMAKE_BUILD_TYPE=RelWithDebInfo `
  -DQLITEMD_CORE_ONLY=ON `
  -DQLITEMD_SVG_BACKEND=resvg
cmake --build build_core_x64 --parallel 2
```

This produces `build_core_x64/bin/qlitemd_mdhtml_c.dll`, `resvg.dll`,
`ratex_ffi.dll` and, because Windows x64 ships the prebuilt libraries,
`merman_ffi.dll`. Formulas and Mermaid choose their native or QuickJS
backend at render time.
The project copies whichever native files exist after each `dotnet build`
via the `QlitemdNativeDir` property (override it if your build directory
differs).

## Build and run

```powershell
dotnet build -c Release
.\bin\x64\Release\net10.0\AvaloniaMdhtml.exe
```

Screenshot modes used for verification:

```powershell
.\bin\x64\Release\net10.0\AvaloniaMdhtml.exe `
  --screenshot D:\temp\avalonia-light.png
.\bin\x64\Release\net10.0\AvaloniaMdhtml.exe --dark `
  --screenshot D:\temp\avalonia-dark.png
```

## What it validates

| Layer | Path |
| --- | --- |
| Markdown parse/assemble + worker lanes | `qlitemd_mdhtml_c.dll` (`MdHtmlPipeline`) |
| C ABI | `mdhtml_render_async`, `mdhtml_job_wait`, assets/code blocks |
| HTML rendering | `LiteHtmlSharp.Avalonia` 2.0.7 |
| Formula/Mermaid pixels | SVG → `Svg.Skia` → PNG → litehtml image loader |
| Theme | `resources/themes/*.css` plus `base.css` (litehtml UA rules) |

## Known limitations of the validation setup

- `LiteHtmlSharp.Avalonia` has no text selection, clipboard selection or
  find; this sample only validates rendering, links and the code-copy link.
- Inline `<img>` elements are not loaded by the 2.0.7 Avalonia backend; the
  sample's base CSS makes images block-level. Inline formulas therefore
  appear on their own line.
- litehtml needs a base stylesheet with `display:block`/table rules; that is
  why `base.css` exists in addition to the qlitemd theme CSS.
- Not a cross-platform product matrix: this harness is Windows x64 only.
