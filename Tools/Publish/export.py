#!/usr/bin/env python3
"""Export Source/JaiScript as the public JaiScript repository.

  export.py stage   [--commit C] [--staging DIR] [--release-date TEXT]
      Steps 1-7: archive the commit's tracked package files, apply the allowlist, fill the release date,
      strip internal/ citations from C/C++ comments (token-identical check), then the leak scan, link check,
      binary/size check and the site checks. Writes export-manifest.json and export-report.txt in the staging
      directory's parent. Exit 0 only when every check passes.
  export.py gate NAME [--staging DIR]
      One step-8 gate in the staging tree (configure, build-core, build-rest, build-debug, tests-release,
      tests-debug, ci, site). Run them in that order; each fits one timing-lock hold.
  export.py publish --staging DIR --target DIR --version TEXT --confirm
      Step 9: git init the target if needed, replace its tree with the staging tree and make one commit.
      Refuses without --confirm.

Bindstone stays the development tree; each release is one export and one commit.
"""
import argparse
import datetime
import fnmatch
import hashlib
import io
import json
import os
import re
import shutil
import subprocess
import sys
import tarfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
PACKAGE = "Source/JaiScript"
DEFAULT_STAGING = os.path.abspath(os.path.join(REPO, "..", "jaiscript-staging"))

CPP_EXT = (".cpp", ".hpp", ".h", ".c", ".cc", ".inl")
BINARY_OK_EXT = (".png", ".jpg", ".jpeg", ".gif", ".ico", ".woff", ".woff2", ".ttf", ".otf")
SIZE_LIMIT = 1024 * 1024
SIZE_EXCEPTIONS = ("docs/site/assets/search-index.js", "docs/site/reference/api-catalog.json")

PUBLIC_GITIGNORE = """out/
build/
build-*/
.vs/
.vscode/
*.jaibite
__pycache__/
*.obj
*.o
*.pdb
*.ilk
*.exe
*.dll
*.lib
*.exp
*.log
*.tmp
*.zip
*.wad
node_modules/
*.vsix
"""

LEAK_CHECKS = [
    ("host-project", re.compile(r"Bindstone|MutedVision|\bMV::|\bMV/")),
    ("machine-path", re.compile(r"\b[A-Za-z]:[\\/](?:git|Users|Program Files)|/d/git\b|C:\\\\Users|Source/JaiScript|out/astra|JaiScriptComparisons")),
    ("provenance", re.compile(r"\bDev (?:ruling|request|direction|decision|approved|ruled)|\bDev's\b|\(Dev\b|\bper Dev\b")),
    ("agent-name", re.compile(r"\b(?:Astra|Fable|Claude)\b")),
    ("ledger", re.compile(r"\bcampaign\b|\bledger\b|\brows? \d+[a-z]?(?:\.\d+)?\b")),
    ("iso-date", re.compile(r"\b20\d\d-[01]\d-[0-3]\d\b")),
    ("internal-ref", re.compile(r"(?<![\w.])\.internal/|(?<![\w./])internal/[\w./-]+\.md")),
    ("email", re.compile(r"\b[\w.+-]+@[A-Za-z0-9-]+\.[A-Za-z]{2,}\b")),
    ("secret", re.compile(r"-----BEGIN [A-Z ]*PRIVATE KEY-----|\bAKIA[0-9A-Z]{16}\b|\bghp_[A-Za-z0-9]{36}\b|\bxox[baprs]-[A-Za-z0-9-]+")),
]

# ---------------------------------------------------------------------------------------------------- utilities

def die(msg):
    print("export: " + msg, file=sys.stderr)
    sys.exit(1)

def glob_regex(glob):
    out, i = "", 0
    while i < len(glob):
        if glob.startswith("**/", i):
            out += "(?:.*/)?"; i += 3
        elif glob.startswith("**", i):
            out += ".*"; i += 2
        elif glob[i] == "*":
            out += "[^/]*"; i += 1
        elif glob[i] == "?":
            out += "[^/]"; i += 1
        else:
            out += re.escape(glob[i]); i += 1
    return re.compile("^" + out + "$")

def read_rules(path):
    rules = []
    for line in open(path, encoding="utf-8"):
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        sign, glob = line[0], line[1:].strip()
        if sign not in "+-":
            die("bad allowlist line: " + line)
        rules.append((sign, glob, glob_regex(glob)))
    return rules

def read_exceptions(path):
    out = []
    for line in open(path, encoding="utf-8"):
        if not line.strip() or line.startswith("#"):
            continue
        parts = line.rstrip("\n").split("\t")
        if len(parts) < 3:
            die("bad exception line: " + line)
        out.append((glob_regex(parts[0]), parts[1], parts[2]))
    return out

def is_text(data):
    if b"\0" in data:
        return False
    try:
        data.decode("utf-8")
        return True
    except UnicodeDecodeError:
        return False

# ---------------------------------------------------------------------------------------------------- C++ comments

def split_cpp(text):
    """Segments of (kind, text), kind in code/comment/string, so comments can change and code is untouched."""
    segs, i, n, code = [], 0, len(text), []
    def flush():
        if code:
            segs.append(("code", "".join(code))); code.clear()
    while i < n:
        c = text[i]
        if text.startswith("//", i):
            j = text.find("\n", i)
            j = n if j < 0 else j
            flush(); segs.append(("comment", text[i:j])); i = j
        elif text.startswith("/*", i):
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            flush(); segs.append(("comment", text[i:j])); i = j
        elif text.startswith('R"', i) and (i == 0 or not (text[i - 1].isalnum() or text[i - 1] == "_")):
            m = re.match(r'R"([^(\s\\]*)\(', text[i:])
            if not m:
                code.append(c); i += 1; continue
            end = text.find(")" + m.group(1) + '"', i)
            end = n if end < 0 else end + len(m.group(1)) + 2
            flush(); segs.append(("string", text[i:end])); i = end
        elif c in "\"'":
            if c == "'" and i > 0 and (text[i - 1].isalnum()) and i + 1 < n and text[i + 1].isalnum():
                code.append(c); i += 1; continue          # a digit separator (1'000)
            j = i + 1
            while j < n and text[j] != c and text[j] != "\n":
                j += 2 if text[j] == "\\" else 1
            flush(); segs.append(("string", text[i:j + 1])); i = j + 1
        else:
            code.append(c); i += 1
    flush()
    return segs

CITE = r"internal/[\w./-]+\.md"
SECTION = r"(?:\s*(?:§\s*)?[\w.]+(?:\s*\"[^\"]*\")?|\s*\"[^\"]*\"|\s*\([^()]*\))*"
STRIP_RULES = [
    re.compile(r"\s*\((?:see\s+)?" + CITE + r"[^()]*\)"),                          # (internal/x.md ...)
    re.compile(r"\s*[;,]\s*(?:see\s+)?" + CITE + r"[^();,]*(?=\))"),                 # (... ; internal/x.md ...)
    re.compile(r"\s*[;,]\s*(?:see\s+)?" + CITE + r"[^.;\n]*"),                       # ..., internal/x.md ...
    re.compile(r"\s*\b[Ss]ee\s+" + CITE + r"[^.\n]*\.?"),                            # See internal/x.md ...
    re.compile(r"\s*\b(?:[Ii]n|[Pp]er|[Ff]rom|[Ff]ollows)\s+" + CITE + SECTION),     # in internal/x.md 3.2
    re.compile(r"\s*" + CITE + SECTION + r"\s*[:;,-]?"),                               # bare citation
]

def strip_comment(comment):
    out = comment
    for rule in STRIP_RULES:
        out = rule.sub("", out)
    out = re.sub(r"\(\s*\)", "", out)
    return out

def strip_cpp_citations(text):
    segs = split_cpp(text)
    changed = 0
    out = []
    for kind, s in segs:
        if kind == "comment" and "internal/" in s:
            new = strip_comment(s)
            if new != s:
                changed += 1
                if new.startswith("//") and not new[2:].strip():
                    new = None                       # the comment held only the citation: drop it
            s = new
        if s is not None:
            out.append(s)
    result = "".join(out)
    result = re.sub(r"[ \t]+(\r?\n)", r"\1", result) if changed else result
    # a line left blank by a dropped whole-line comment goes too
    if changed:
        orig_lines = text.split("\n")
        new_lines = result.split("\n")
        if len(new_lines) == len(orig_lines):
            result = "\n".join(l for l, o in zip(new_lines, orig_lines) if not (l.strip() == "" and o.strip() != ""))
    return result, changed

def code_tokens(text):
    return re.sub(r"\s+", " ", "".join(s for k, s in split_cpp(text) if k != "comment")).strip()

# ---------------------------------------------------------------------------------------------------- stage

def git(*args, binary=False):
    r = subprocess.run(["git", "-C", REPO] + list(args), capture_output=True)
    if r.returncode != 0:
        die("git " + " ".join(args) + ": " + r.stderr.decode(errors="replace"))
    return r.stdout if binary else r.stdout.decode()

def stage(args):
    commit = git("rev-parse", args.commit).strip()
    staging = os.path.abspath(args.staging)
    report, problems = [], []
    def note(line):
        report.append(line); print(line)
    def fail(line):
        problems.append(line); report.append("FAIL " + line); print("FAIL " + line)

    # 1. archive the commit's tracked package files
    if os.path.exists(staging):
        shutil.rmtree(staging)
    os.makedirs(staging)
    tar = tarfile.open(fileobj=io.BytesIO(git("archive", "--format=tar", commit, PACKAGE, binary=True)))
    files = {}
    for m in tar.getmembers():
        if m.isfile():
            rel = m.name[len(PACKAGE) + 1:]
            files[rel] = tar.extractfile(m).read()
    note(f"source {commit}: {len(files)} tracked files under {PACKAGE}")

    # 2. allowlist, deny by default
    rules = read_rules(os.path.join(HERE, "allowlist.txt"))
    kept, excluded, unmatched = {}, [], []
    for rel in sorted(files):
        inc = any(s == "+" and r.match(rel) for s, g, r in rules)
        exc = any(s == "-" and r.match(rel) for s, g, r in rules)
        if exc:
            excluded.append(rel)
        elif inc:
            kept[rel] = files[rel]
        else:
            unmatched.append(rel)
    for rel in unmatched:
        fail(f"allowlist: no rule covers {rel}")
    note(f"allowlist: {len(kept)} kept, {len(excluded)} excluded, {len(unmatched)} unmatched")

    # 3. release date, then the comment strip with its token-identical check
    date = args.release_date or datetime.date.today().strftime("%d %B %Y").lstrip("0")
    stripped_files, stripped_comments = 0, 0
    for rel, data in list(kept.items()):
        if not is_text(data):
            continue
        text = data.decode("utf-8")
        new = text.replace("@RELEASE_DATE@", date)
        if rel.endswith(CPP_EXT) and "internal/" in new:
            out, n = strip_cpp_citations(new)
            if n:
                if code_tokens(out) != code_tokens(new):
                    fail(f"comment strip changed code in {rel}")
                    continue
                stripped_files += 1; stripped_comments += n
                new = out
        if new != text:
            kept[rel] = new.encode("utf-8")
    note(f"release date: {date}; internal/ citations stripped from {stripped_comments} comments in {stripped_files} files")

    for rel, data in kept.items():
        path = os.path.join(staging, rel.replace("/", os.sep))
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "wb") as f:
            f.write(data)
    with open(os.path.join(staging, ".gitignore"), "w", newline="\n") as f:
        f.write(PUBLIC_GITIGNORE)

    # 4. leak scan of every text file
    exceptions = read_exceptions(os.path.join(HERE, "leak_exceptions.txt"))
    leaks = 0
    for rel, data in sorted(kept.items()):
        if not is_text(data):
            continue
        for lineno, line in enumerate(data.decode("utf-8").splitlines(), 1):
            for cid, rx in LEAK_CHECKS:
                for m in rx.finditer(line):
                    if any(g.match(rel) and cid == c and t in line for g, c, t in exceptions):
                        continue
                    leaks += 1
                    fail(f"leak {cid}: {rel}:{lineno}: {line.strip()[:160]}")
    note(f"leak scan: {leaks} hits")

    # 5. link check (Markdown everywhere; the site has its own checker in step 7)
    broken = 0
    for rel, data in sorted(kept.items()):
        if not rel.endswith(".md"):
            continue
        for m in re.finditer(r"\[[^\]]*\]\(([^)\s]+)\)", data.decode("utf-8")):
            target = m.group(1).split("#")[0]
            if not target or re.match(r"^[a-z]+:", target):
                continue
            resolved = os.path.normpath(os.path.join(os.path.dirname(rel), target)).replace(os.sep, "/")
            if resolved.startswith("..") or not (resolved in kept or any(k.startswith(resolved.rstrip("/") + "/") for k in kept)):
                broken += 1
                fail(f"link: {rel} -> {m.group(1)}")
    note(f"links: {broken} broken")

    # 6. binary and size check
    total = 0
    for rel, data in sorted(kept.items()):
        total += len(data)
        if not is_text(data) and not rel.lower().endswith(BINARY_OK_EXT) and os.path.basename(rel) != ".nojekyll":
            fail(f"binary: {rel} ({len(data)} bytes)")
        if len(data) > SIZE_LIMIT and rel not in SIZE_EXCEPTIONS:
            fail(f"size: {rel} is {len(data)} bytes")
    note(f"size: {len(kept)} files, {total / 1048576:.1f} MiB")

    # 7. generated files fresh and the site checks, in the staging tree
    site = os.path.join(staging, "docs", "site")
    r = subprocess.run([sys.executable, "../site_tools/verify_site.py", "--no-samples"], cwd=site, capture_output=True, text=True)
    for line in r.stdout.splitlines():
        if line.startswith("-- ") or line.startswith("failed:"):
            note("site " + line)
    if r.returncode != 0:
        fail("site checks failed in staging")

    manifest = {
        "source_commit": commit,
        "release_date": date,
        "files": {rel: hashlib.sha256(data).hexdigest() for rel, data in sorted(kept.items())},
        "excluded": excluded,
    }
    parent = os.path.dirname(staging)
    with open(os.path.join(parent, "export-manifest.json"), "w") as f:
        json.dump(manifest, f, indent=1)
    with open(os.path.join(parent, "export-report.txt"), "w", encoding="utf-8") as f:
        f.write("\n".join(report) + "\n")
    print(f"export: {'OK' if not problems else str(len(problems)) + ' problem(s)'}; staging at {staging}")
    sys.exit(0 if not problems else 1)

# ---------------------------------------------------------------------------------------------------- gates

def run(cmd, cwd):
    print("+ " + " ".join(cmd), flush=True)
    r = subprocess.run(cmd, cwd=cwd)
    if r.returncode != 0:
        die(f"gate step failed ({r.returncode}): {' '.join(cmd)}")

GATES = {
    "configure":     [["cmake", "--preset", "release", "-G", "Ninja"], ["cmake", "--preset", "debug", "-G", "Ninja"]],
    "build-core":    [["cmake", "--build", "build", "--target", "jaiscript_tests", "jaiscript_runner"]],
    "build-rest":    [["cmake", "--build", "build"]],
    "build-debug":   [["cmake", "--build", "build-debug", "--target", "jaiscript_tests"]],
    "tests-release": [["build/bin/jaiscript_tests"], ["build/bin/jaiscript_tests", "--backend=vm"]],
    "tests-debug":   [["build-debug/bin/jaiscript_tests"], ["build-debug/bin/jaiscript_tests", "--backend=vm"]],
    "ci":            [["bash", "tools/ci/gates.sh", "fuzz", "gloom", "demoreel", "crawler", "gatorquest", "embed-quickstart", "lsp"]],
    "site":          [[sys.executable, "../site_tools/verify_site.py", "--runner", "../../build/bin/jaiscript"]],
}

def gate(args):
    staging = os.path.abspath(args.staging)
    if args.name not in GATES:
        die("unknown gate " + args.name + " (" + ", ".join(GATES) + ")")
    cwd = os.path.join(staging, "docs", "site") if args.name == "site" else staging
    env_bin = os.path.join(staging, "build", "bin")
    os.environ["BIN"] = env_bin
    for cmd in GATES[args.name]:
        if cmd[0].startswith("build") and os.name == "nt":
            cmd = [os.path.join(staging, cmd[0].replace("/", os.sep)) + ".exe"] + cmd[1:]
        run(cmd, cwd)
    print(f"gate {args.name}: OK")

# ---------------------------------------------------------------------------------------------------- publish

def publish(args):
    if not args.confirm:
        die("publish needs --confirm (it creates or commits to " + args.target + ")")
    staging, target = os.path.abspath(args.staging), os.path.abspath(args.target)
    if not os.path.isdir(os.path.join(target, ".git")):
        os.makedirs(target, exist_ok=True)
        subprocess.run(["git", "init", target], check=True)
    for name in os.listdir(target):
        if name != ".git":
            p = os.path.join(target, name)
            shutil.rmtree(p) if os.path.isdir(p) else os.remove(p)
    for name in os.listdir(staging):
        if name in ("build", "build-debug"):
            continue
        src, dst = os.path.join(staging, name), os.path.join(target, name)
        shutil.copytree(src, dst) if os.path.isdir(src) else shutil.copy2(src, dst)
    subprocess.run(["git", "-C", target, "add", "-A"], check=True)
    subprocess.run(["git", "-C", target, "commit", "-q", "-m", "JaiScript " + args.version], check=True)
    print("published " + args.version + " to " + target)

def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    s = sub.add_parser("stage")
    s.add_argument("--commit", default="HEAD")
    s.add_argument("--staging", default=DEFAULT_STAGING)
    s.add_argument("--release-date")
    g = sub.add_parser("gate")
    g.add_argument("name")
    g.add_argument("--staging", default=DEFAULT_STAGING)
    p = sub.add_parser("publish")
    p.add_argument("--staging", default=DEFAULT_STAGING)
    p.add_argument("--target", required=True)
    p.add_argument("--version", required=True)
    p.add_argument("--confirm", action="store_true")
    args = ap.parse_args()
    {"stage": stage, "gate": gate, "publish": publish}[args.cmd](args)

if __name__ == "__main__":
    main()
