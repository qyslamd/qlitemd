# Contributing

## Build and test

Use the Qt kit that matches the change you are testing. The primary
configuration is Qt 5.15.2 x86 with Visual Studio 2019.

```bash
cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_PREFIX_PATH=/path/to/Qt
cmake --build build
ctest --test-dir build --output-on-failure
```

Keep `QLITEMD_BUILD_TESTS=ON` for code changes. Run at least the
`MARKDOWN` test group before opening a pull request.

## Code style

- C++17, two-space indentation, Google C++ Style naming
- Header guards use `#define`, never `#pragma once`
- No `using namespace std`
- Avoid RTTI and C-style casts
- Keep public headers free of qlitehtml and QuickJS types
- Add a test for behavior changes that can run headlessly

## Third-party patches

Vendored dependencies live under `3rdparty/`. Do not silently edit them:
record the change in `patches/` and document the upstream version. A
dependency update needs a fresh build of the library, tests and
`md_viewer`.

## Licensing

qlitemd's own code is MIT. Do not add files that cannot be distributed
under the repository's licenses. In particular, do not add the Microsoft
Segoe Fluent Icons font or other proprietary assets.
