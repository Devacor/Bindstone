#!/usr/bin/env python3
"""Run every site check in one go.

    python assets/tools/verify_site.py [--no-samples] [--runner PATH]

Order: script syntax (node --check), links and anchors, HTML structure, internal leakage and stale facts, retired error
texts, search index freshness, chrome freshness, then (unless --no-samples) every jaiscript
sample on both backends under strict, with its expected output. Exit code 1 if any check fails.
"""

import pathlib
import shutil
import subprocess
import sys

HERE = pathlib.Path(__file__).resolve().parent

STEPS = [
    ("links", ["check_links.py"]),
    ("html", ["check_html.py"]),
    ("leakage / stale facts", ["check_leakage.py"]),
    ("retired error texts", ["check_old_texts.py"]),
    ("search index", ["build_search_index.py", "--check"]),
    ("chrome", ["apply_chrome.py", "--check"]),
    ("samples", ["run_samples.py"]),
]


def main():
    skip_samples = "--no-samples" in sys.argv
    runner = []
    if "--runner" in sys.argv:
        runner = ["--runner", str(pathlib.Path(sys.argv[sys.argv.index("--runner") + 1]).resolve())]
    failed = []
    print("=" * 72)
    print("== scripts")
    node = shutil.which("node")
    if node:
        bad = [js.name for js in sorted((HERE.parent).glob("*.js"))
               if subprocess.call([node, "--check", str(js)]) != 0]
        print("-- scripts: %s" % ("FAILED (%s)" % ", ".join(bad) if bad else "ok"))
        if bad:
            failed.append("scripts")
    else:
        print("-- scripts: skipped (node not on PATH)")
    for name, cmd in STEPS:
        if name == "samples" and skip_samples:
            continue
        print("=" * 72)
        print("== " + name)
        extra = runner if name == "samples" else []
        rc = subprocess.call([sys.executable, str(HERE / cmd[0])] + cmd[1:] + extra, cwd=str(HERE.parents[1]))
        print("-- %s: %s" % (name, "ok" if rc == 0 else "FAILED (exit %d)" % rc))
        if rc:
            failed.append(name)
    print("=" * 72)
    print("failed: %s" % (", ".join(failed) if failed else "none"))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
