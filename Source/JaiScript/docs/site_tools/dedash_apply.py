#!/usr/bin/env python3
"""Apply hand-written (old, new) prose rewrites to one page.

    python site_tools/dedash_apply.py edits.json

edits.json: {"page": "guide/01-hello.html", "edits": [["old", "new"], ...]}

Every old string must occur exactly once in the file, and must not overlap a <code>/<pre>
span. Line endings are preserved byte for byte; nothing is written unless every edit passes.
"""

import json
import pathlib
import re
import sys

SITE = pathlib.Path(__file__).resolve().parent.parent / "site"
BLOCK = re.compile(r"<(code|pre)\b.*?</\1>", re.IGNORECASE | re.DOTALL)
DASH = re.compile(r"—|&mdash;|&#x2014;|&#8212;", re.IGNORECASE)


def code_spans(text):
    return [m.group(0) for m in BLOCK.finditer(text)]


def main():
    spec = json.loads(pathlib.Path(sys.argv[1]).read_text(encoding="utf-8"))
    page = SITE / spec["page"]
    with open(page, "r", encoding="utf-8", newline="") as fh:
        text = fh.read()
    guarded = [(m.start(), m.end()) for m in BLOCK.finditer(text)]

    problems = []
    for i, (old, new) in enumerate(spec["edits"]):
        n = text.count(old)
        if n != 1:
            problems.append(f"edit {i}: old occurs {n}x: {old[:90]!r}")
            continue
        at = text.index(old)
        if any(s < at and at < e or s < at + len(old) and at + len(old) < e for s, e in guarded):
            problems.append(f"edit {i}: starts or ends inside a code/pre span: {old[:90]!r}")
            continue
        if code_spans(old) != code_spans(new):
            problems.append(f"edit {i}: changes code/pre content: {old[:90]!r}")
            continue
        if DASH.search(new):
            problems.append(f"edit {i}: replacement still has an em-dash: {new[:90]!r}")
    if problems:
        print("\n".join(problems))
        return 1

    for old, new in spec["edits"]:
        text = text.replace(old, new, 1)
    with open(page, "w", encoding="utf-8", newline="") as fh:
        fh.write(text)
    print(f"{spec['page']}: applied {len(spec['edits'])} edits")
    return 0


if __name__ == "__main__":
    sys.exit(main())
