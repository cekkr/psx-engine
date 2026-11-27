#!/usr/bin/env python3
"""Check the local inline Markdown links and heading anchors used in this repo."""

import os
import re
import subprocess
from collections import Counter
from pathlib import Path
from urllib.parse import unquote, urlsplit


ROOT = Path(__file__).resolve().parent.parent
LINK = re.compile(r"\]\(\s*(?:<([^>]+)>|([^\s)]+))(?:\s+['\"][^)]*['\"])?\s*\)")
FENCE = re.compile(r"^ {0,3}(`{3,}|~{3,})")
HEADING = re.compile(r"^ {0,3}#{1,6}\s+(.*?)\s*#*\s*$")


def markdown_lines(path):
    fence = ""
    for number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        match = FENCE.match(line)
        if match:
            marker = match[1]
            if not fence:
                fence = marker
            elif marker.startswith(fence) and not line[match.end():].strip():
                fence = ""
        elif not fence:
            yield number, line


def headings(path):
    used = set()
    duplicates = Counter()
    for _, line in markdown_lines(path):
        match = HEADING.match(line)
        if match:
            base = re.sub(r"[^\w\s-]", "", match[1].lower()).replace(" ", "-")
            slug = base
            while slug in used:
                duplicates[base] += 1
                slug = f"{base}-{duplicates[base]}"
            used.add(slug)
    return used


def main():
    paths = subprocess.check_output(
        ["git", "ls-files", "-z", "--cached", "--others", "--exclude-standard", "--", "*.md"],
        cwd=ROOT,
    )
    anchors = {}
    failed = False
    for name in sorted(set(paths.split(b"\0")) - {b""}):
        path = ROOT / os.fsdecode(name)
        if not path.is_file():
            continue
        for number, line in markdown_lines(path):
            for link in LINK.finditer(line):
                url = urlsplit(link[1] or link[2])
                if url.scheme or url.netloc:
                    continue
                destination = path
                if url.path:
                    destination = (path.parent / unquote(url.path)).resolve()
                if not destination.exists() or not destination.is_relative_to(ROOT):
                    problem = "missing local target"
                elif url.fragment and destination.suffix.lower() == ".md":
                    if destination not in anchors:
                        anchors[destination] = headings(destination)
                    if unquote(url.fragment) in anchors[destination]:
                        continue
                    problem = "missing Markdown heading"
                else:
                    continue
                print(f"{path.relative_to(ROOT)}:{number}: {problem}: {link[1] or link[2]}")
                failed = True
    if not failed:
        print("Local Markdown links passed.")
    return int(failed)


if __name__ == "__main__":
    raise SystemExit(main())
