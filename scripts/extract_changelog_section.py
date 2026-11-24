#!/usr/bin/env python3
"""Extract one version section of CHANGELOG.md for a GitHub release body.

The release workflow passes the tag version; the script prints the matching
`## <version>` section (without the heading) to the output file.
"""

import argparse
import re
import sys
from pathlib import Path


def extract(text, version):
    heading = re.compile(
        r"^##\s+v?" + re.escape(version) + r"(?:\s|$).*$", re.MULTILINE)
    match = heading.search(text)
    if match is None:
        raise SystemExit(
            "CHANGELOG.md has no section for version %s" % version)
    start = match.end()
    next_heading = re.compile(r"^##\s+", re.MULTILINE).search(text, start)
    end = next_heading.start() if next_heading else len(text)
    body = text[start:end].strip()
    if not body:
        raise SystemExit("CHANGELOG.md section for %s is empty" % version)
    return body + "\n"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--version", required=True)
    parser.add_argument("--changelog", default="CHANGELOG.md", type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()

    text = args.changelog.read_text(encoding="utf-8")
    body = extract(text, args.version)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(body, encoding="utf-8")
    print("wrote %s (%d bytes)" % (args.output, len(body.encode("utf-8"))))
    return 0


if __name__ == "__main__":
    sys.exit(main())
