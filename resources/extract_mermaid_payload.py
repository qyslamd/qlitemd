# Mermaid 载荷的重建配方（本目录三份 JS 都是从 mermaidx-0.9.5 取来的）。
#
# 为什么入库：三份都是纯 JS、与平台无关，渲染时由 qjsc 在构建期编成字节码
# （见 src/markdown/CMakeLists.txt），部署不用跟着拷文件，两个平台也不用各装
# 一套 node。代价是不可读、不可 review，所以配方必须一起提交。
#
#   mermaid.js     官方 mermaid bundle（内含 mermaid 11.16.0）  3565102 字节
#   dom_shim.js    假的 DOM/SVG 环境                            53972 字节
#   path_bbox.js   mermaidx 的 PATH_BBOX_JS 常量                 4823 字节
#
# 为什么不直接 import mermaidx：它会拉可选依赖（termaid 等），只为抠一段字符串
# 不值得装整个包；PATH_BBOX_JS 在 path_bbox.py 里就是个 r"""...""" 常量。
#
# 用法（升级 mermaidx 时）：
#   python extract_mermaid_payload.py <mermaidx 目录>          # 重新生成
#   python extract_mermaid_payload.py <mermaidx 目录> --check  # 只对哈希
#
# 注意：mermaid 的版本看 bundle 里的 version:"11.x.y"，不是 mermaidx 的版本号，
# 两者独立升级。
#
# 换行：上游 mermaidx 的这三份就是 CRLF（mermaidx 的 .py 也是 CRLF），本目录
# 照搬不改字节，所以 .gitattributes 里给它们标了 -text —— 不标的话 git 会把仓库
# 里存成 LF、两个平台 checkout 出不同字节，下面的哈希在两个平台上就对不上。
# 读文件一律用 bytes（read_bytes / read_bytes().decode()），别用 read_text()：
# 它会做 universal newlines，把 CRLF 悄悄变成 LF。

import hashlib
import pathlib
import re
import sys

# path_bbox.js 是 LF（4690 字节）：上游那个 JS 字面量本身是 LF。上游示例里
# 那份是 4823 字节的 CRLF，因为它是用 PowerShell 的 write_text() 写出来的，
# 会被翻成 \r\n —— 以字面量为准，别照抄示例那份的字节数。
EXPECTED = {
    "mermaid.js": "1dd12fadbfa258c3ac600e9e7f1d6a3d",
    "dom_shim.js": "d8d380c55772e2868cdd075d4c42a5c5",
    "path_bbox.js": "8f82e27f69973c1e31fd488dfa67435e",
}

HERE = pathlib.Path(__file__).resolve().parent


def md5(data: bytes) -> str:
    return hashlib.md5(data).hexdigest()


def build(mermaidx: pathlib.Path) -> dict[str, bytes]:
    assets = mermaidx / "mermaidx" / "assets"
    payload = {
        "mermaid.js": (assets / "mermaid.js").read_bytes(),
        "dom_shim.js": (assets / "dom_shim.js").read_bytes(),
    }
    # read_bytes + decode：保住 CRLF（read_text 会顺手转成 LF）
    py = (mermaidx / "mermaidx" / "path_bbox.py").read_bytes().decode("utf-8")
    match = re.search(r'PATH_BBOX_JS\s*=\s*r?"""(.*?)"""', py, re.S)
    if match is None:
        raise SystemExit("path_bbox.py 里没找到 PATH_BBOX_JS 常量，上游可能改结构了")
    payload["path_bbox.js"] = match.group(1).encode("utf-8")
    return payload


def main() -> int:
    if len(sys.argv) < 2:
        print(__doc__ or "用法见本文件头部注释")
        return 2
    check_only = "--check" in sys.argv
    mermaidx = pathlib.Path(sys.argv[1]).resolve()
    if not mermaidx.is_dir():
        raise SystemExit(f"不是目录：{mermaidx}")

    payload = build(mermaidx)
    bad = 0
    for name, data in payload.items():
        digest = md5(data)
        expected = EXPECTED.get(name)
        state = "OK" if digest == expected else "变了"
        print(f"{name:16} {len(data):>9} 字节  md5={digest}  [{state}]")
        if digest != expected:
            bad += 1
            if check_only:
                continue
        if not check_only:
            (HERE / name).write_bytes(data)
    if bad and check_only:
        print("哈希与仓库里的不一致：确认是有意升级再重跑（去掉 --check）")
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
