#!/usr/bin/env bash
# Runs inside an arm64 Ubuntu 20.04 container (QEMU on an x64 runner).
# Qt comes from Focal's apt archive: 5.12.8, the oldest supported kit.
set -euxo pipefail

export DEBIAN_FRONTEND=noninteractive

apt-get update
apt-get install -y --no-install-recommends \
  build-essential \
  ca-certificates \
  ninja-build \
  python3-pip \
  qtchooser \
  qtbase5-dev \
  qtbase5-dev-tools \
  qt5-qmake \
  libqt5svg5-dev \
  libgl1-mesa-dev \
  libglu1-mesa-dev

# Focal ships CMake 3.16; the project requires 3.19.
python3 -m pip install --no-cache-dir --disable-pip-version-check \
  "cmake==3.27.9"
cmake --version
qmake --version || true

# No linux-arm64 native backends in the tree yet: use the portable
# QuickJS + QtSvg path, which is also what CMake would fall back to.
cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DQLITEMD_QT_MAJOR=5 \
  -DQLITEMD_BUILD_MD_VIEWER=OFF \
  -DQLITEMD_SVG_BACKEND=qtsvg \
  -DQLITEMD_FORMULA_BACKEND=quickjs

cmake --build build --parallel 2
QT_QPA_PLATFORM=offscreen ctest --test-dir build \
  --output-on-failure -L MARKDOWN
