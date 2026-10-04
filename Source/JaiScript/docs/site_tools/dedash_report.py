#!/usr/bin/env python3
"""Find every em-dash that lives in PROSE (outside <code> and <pre>).

    python site_tools/dedash_report.py            # full listing, context per hit
    python site_tools/dedash_report.py --counts   # per-page totals only
    python site_tools/dedash_report.py PATH ...   # limit to some pages

Code spans keep their dashes; only prose is reported. Exit code 1 when prose hits remain.
"""

import pathlib
import re
import sys

SITE = pathlib.Path(__file__).resolve().parent.parent / "site"
DASH = re.compile(r"—|&mdash;|&#x2014;|&#8212;", re.IGNORECASE)
BLOCK = re.compile(r"<(code|pre)\b.*?</\1>|<(code|pre)\b[^>]*/>", re.IGNORECASE | re.DOTALL)


def masked(text):
    """Same length as text, with <code>/<pre> spans blanked out."""
    out = list(text)
    for m in BLOCK.finditer(text):
        for i in range(m.start(), m.end()):
            if out[i] != "\n":
                out[i] = " "
    return "".join(out)


HEAD = re.compile(r"<head\b.*?</head>", re.IGNORECASE | re.DOTALL)


def hits(path, skip_head=True):
    raw = path.read_text(encoding="utf-8")
    body = masked(raw)
    if skip_head:
        m = HEAD.search(body)
        if m:
            body = body[:m.start()] + re.sub(r"[^\n]", " ", m.group(0)) + body[m.end():]
    starts = [0]
    for m in re.finditer("\n", raw):
        starts.append(m.end())
    found = []
    for m in DASH.finditer(body):
        line_no = len([s for s in starts if s <= m.start()])
        line_start = starts[line_no - 1]
        line_end = raw.find("\n", m.start())
        if line_end == -1:
            line_end = len(raw)
        line = raw[line_start:line_end]
        col = m.start() - line_start
        lo, hi = max(0, col - 80), min(len(line), col + 80)
        found.append((line_no, ("..." if lo else "") + line[lo:hi] + ("..." if hi < len(line) else "")))
    return found


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    counts_only = "--counts" in sys.argv
    skip_head = "--with-head" not in sys.argv
    if args:
        pages = [pathlib.Path(a) if pathlib.Path(a).is_absolute() else SITE / a for a in args]
    else:
        pages = sorted(SITE.glob("*.html")) + sorted(SITE.glob("*/*.html"))
    total = 0
    for page in pages:
        found = hits(page, skip_head)
        total += len(found)
        rel = page.relative_to(SITE).as_posix()
        if counts_only:
            if found:
                print(f"{len(found):4d}  {rel}")
            continue
        if not found:
            continue
        print(f"\n=== {rel}  ({len(found)}) ===")
        for line_no, ctx in found:
            print(f"{line_no:5d}: {ctx}")
    print(f"\nTOTAL prose em-dashes: {total}")
    return 1 if total else 0


if __name__ == "__main__":
    sys.exit(main())
