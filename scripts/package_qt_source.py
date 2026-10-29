#!/usr/bin/env python3
"""Build the qlitemd Qt widget source package.

The package is consumed with add_subdirectory(), not find_package(). It
contains the Qt widget, the zero-Qt core source, the resources and native
backends they need, but not the md_viewer application shell.
"""

import argparse
import hashlib
import re
import shutil
import subprocess
import sys
import zipfile
from pathlib import Path

LANGUAGES = [
    "bash",
    "batch",
    "c",
    "cpp",
    "css",
    "go",
    "html",
    "java",
    "javascript",
    "json",
    "python",
    "rust",
]


def project_version(root: Path) -> str:
    text = (root / "CMakeLists.txt").read_text(encoding="utf-8")
    match = re.search(r"project\(qlitemd VERSION ([0-9.]+)", text)
    if not match:
        raise SystemExit("cannot read qlitemd version from CMakeLists.txt")
    return match.group(1)


def git_commit(root: Path) -> str:
    try:
        return subprocess.check_output(
            ["git", "-C", str(root), "rev-parse", "HEAD"],
            text=True,
        ).strip()
    except (OSError, subprocess.CalledProcessError):
        return "unknown"


def copy_file(src: Path, dst: Path) -> None:
    dst.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(src, dst)


def copy_tree(src: Path, dst: Path) -> None:
    if not src.is_dir():
        raise SystemExit(f"missing package input: {src}")
    shutil.copytree(src, dst)


def stage_package(root: Path, stage: Path) -> None:
    if stage.exists():
        shutil.rmtree(stage)
    stage.mkdir(parents=True)

    copy_file(root / "CMakeLists.txt", stage / "CMakeLists.txt")
    copy_file(root / "LICENSE", stage / "LICENSE")
    copy_file(root / "NOTICE", stage / "NOTICE")
    copy_file(root / "README.md", stage / "README.md")
    copy_file(root / "README.zh-CN.md", stage / "README.zh-CN.md")

    copy_tree(root / "cmake", stage / "cmake")
    copy_file(root / "docs" / "qt-widget.md", stage / "docs" / "qt-widget.md")
    copy_tree(root / "licenses", stage / "licenses")
    copy_tree(root / "resources", stage / "resources")
    copy_tree(root / "src" / "mdhtml", stage / "src" / "mdhtml")
    copy_tree(root / "src" / "markdown_view",
              stage / "src" / "markdown_view")
    copy_tree(root / "examples" / "qt_widget_minimal",
              stage / "examples" / "qt_widget_minimal")

    copy_tree(root / "3rdparty" / "md4c", stage / "3rdparty" / "md4c")
    copy_tree(root / "3rdparty" / "litehtml",
              stage / "3rdparty" / "litehtml")
    copy_tree(root / "3rdparty" / "qlitehtml",
              stage / "3rdparty" / "qlitehtml")
    copy_tree(root / "3rdparty" / "tree-sitter",
              stage / "3rdparty" / "tree-sitter")
    copy_tree(root / "3rdparty" / "quickjs-ng",
              stage / "3rdparty" / "quickjs-ng")

    ts_lang = stage / "3rdparty" / "tree-sitter-lang"
    ts_lang.mkdir(parents=True)
    copy_file(root / "3rdparty" / "tree-sitter-lang" / "VERSION.md",
              ts_lang / "VERSION.md")
    for language in LANGUAGES:
        src_lang = root / "3rdparty" / "tree-sitter-lang" / language
        dst_lang = ts_lang / language
        copy_tree(src_lang / "src", dst_lang / "src")
        copy_file(src_lang / "queries" / "highlights.scm",
                  dst_lang / "queries" / "highlights.scm")
        copy_file(src_lang / "LICENSE", dst_lang / "LICENSE")

    for name in ("resvg", "ratex", "merman"):
        src_lib = root / "3rdparty" / name
        dst_lib = stage / "3rdparty" / name
        copy_tree(src_lib / "include", dst_lib / "include")
        copy_tree(src_lib / "bin", dst_lib / "bin")
        for license_name in ("LICENSE", "LICENSE-APACHE", "LICENSE-MIT",
                             "THIRD_PARTY_NOTICES.md", "VERSION.md"):
            license_file = src_lib / license_name
            if license_file.is_file():
                copy_file(license_file, dst_lib / license_name)

    info = f"""qlitemd Qt widget source package
=================================

version: {project_version(root)}
commit: {git_commit(root)}

This package is for add_subdirectory() consumers. It contains the
zero-Qt core, the Qt MarkdownView component and the pinned sources and
native backends needed by both. The md_viewer application shell is not
included.

Quick start:
  cmake -S examples/qt_widget_minimal -B build -G Ninja
    -DCMAKE_PREFIX_PATH=/path/to/Qt
  cmake --build build --parallel

Embedding in another project:
  set(QLITEMD_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(QLITEMD_BUILD_MD_VIEWER OFF CACHE BOOL "" FORCE)
  set(QLITEMD_INSTALL OFF CACHE BOOL "" FORCE)
  add_subdirectory(path/to/qlitemd)
  target_link_libraries(app PRIVATE qlitemd::markdown_view)

The consumer owns the Qt kit. For Qt 6 pass -DQLITEMD_QT_MAJOR=6.
"""
    (stage / "PACKAGE-INFO.txt").write_text(info, encoding="utf-8")


def make_zip(stage: Path, archive: Path) -> None:
    with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED,
                         compresslevel=9) as bundle:
        for path in sorted(stage.rglob("*")):
            if path.is_file():
                bundle.write(path, path.relative_to(stage.parent))


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", default=Path(__file__).resolve().parent.parent,
                        type=Path)
    parser.add_argument("--out", default=Path("dist"), type=Path)
    parser.add_argument("--version", default="")
    args = parser.parse_args()

    root = args.root.resolve()
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    version = args.version or project_version(root)
    stage = out / f"qlitemd-{version}-qt-src"
    stage_package(root, stage)

    archive = out / f"qlitemd-{version}-qt-src.zip"
    make_zip(stage, archive)
    digest = hashlib.sha256(archive.read_bytes()).hexdigest()
    (out / f"{archive.name}.sha256").write_text(
        f"{digest}  {archive.name}\\n", encoding="ascii")
    print(f"wrote {archive}")
    print(f"sha256 {digest}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
