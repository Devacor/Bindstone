#!/usr/bin/env python3
"""Export Source/JaiScriptComparisons as the public jaiscript_benchmarks repository's tree.

  export_benchmarks.py --target DIR [--commit C] [--replace]

Archives the commit's tracked files under Source/JaiScriptComparisons (so builds, fetched runtimes and evidence never
leave), lays them out at the repository root, adds LICENSE (JaiScript's), .gitignore, .gitattributes and .gitmodules
(the jaiscript/ submodule), then leak-scans and link-checks every file before anything is written. --replace clears an
existing target first, keeping its .git and its jaiscript/ submodule checkout. Writes no git history: the caller commits.
"""
import argparse
import io
import os
import re
import shutil
import subprocess
import sys
import tarfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
PROJECT = "Source/JaiScriptComparisons"
sys.path.insert(0, HERE)
import export as package_export

SUBMODULE = "jaiscript"
SUBMODULE_URL = "https://github.com/Devacor/jaiscript.git"

GITIGNORE = """out/
build/
build-*/
third_party/*
!third_party/README.md
__pycache__/
*.pyc
.vs/
.vscode/
.cache/
*.jaibite
"""

GITMODULES = f"""[submodule "{SUBMODULE}"]
\tpath = {SUBMODULE}
\turl = {SUBMODULE_URL}
"""

# The package export's checks, with a machine-path check that allows the project's own name, plus agent wording
LEAK_CHECKS = [(cid, rx) for cid, rx in package_export.LEAK_CHECKS if cid != "machine-path"] + [
	("machine-path", re.compile(r"(?<![A-Za-z])[A-Za-z]:\\\\?(?!Program Files)[A-Za-z]|(?<![A-Za-z])[A-Za-z]:/(?:git|Users)|"
		r"/[cd]/(?:git|Users)\b|Source/JaiScript|External/|out/astra|tempclaudefolders")),
	("agent-wording", re.compile(r"\b(?:sub)?agents?\b|\bworktrees?\b|\bmain session\b|\bhandoffs?\b")),
	("internal-dir", re.compile(r"(?<![\w./])\.?internal/")),
]


def git(*args, binary=False):
	result = subprocess.run(["git", "-C", REPO] + list(args), capture_output=True, check=True)
	return result.stdout if binary else result.stdout.decode("utf-8")


def archived_tree(commit):
	data = git("archive", "--format=tar", commit, PROJECT, binary=True)
	result = {}
	prefix = PROJECT + "/"
	with tarfile.open(fileobj=io.BytesIO(data)) as bundle:
		for member in bundle.getmembers():
			if member.isdir() or member.name.startswith("pax_global_header"):
				continue
			if not member.isfile():
				raise SystemExit("export_benchmarks: not a regular file: " + member.name)
			result[member.name[len(prefix):]] = bundle.extractfile(member).read()
	return result


def leak_scan(tree, exceptions):
	result = []
	for rel, data in sorted(tree.items()):
		if not package_export.is_text(data):
			result.append(f"binary file: {rel}")
			continue
		for lineno, line in enumerate(data.decode("utf-8").splitlines(), 1):
			for cid, rx in LEAK_CHECKS:
				if rx.search(line) and not any(g.match(rel) and cid == c and t in line for g, c, t in exceptions):
					result.append(f"leak {cid}: {rel}:{lineno}: {line.strip()[:160]}")
	return result


def link_check(tree):
	result = []
	for rel, data in sorted(tree.items()):
		if not rel.endswith(".md"):
			continue
		for match in re.finditer(r"\[[^\]]*\]\(([^)\s]+)\)", data.decode("utf-8")):
			target = match.group(1).split("#")[0]
			if not target or re.match(r"^[a-z]+:", target):
				continue
			resolved = os.path.normpath(os.path.join(os.path.dirname(rel), target)).replace(os.sep, "/")
			if resolved not in tree and not any(name.startswith(resolved + "/") for name in tree):
				result.append(f"broken link: {rel}: {match.group(1)}")
	return result


def write_tree(tree, target, replace):
	if os.path.exists(target):
		entries = [name for name in os.listdir(target) if name not in (".git", SUBMODULE)]
		if entries and not replace:
			raise SystemExit("export_benchmarks: the target is not empty; pass --replace to rewrite it")
		for name in entries:
			path = os.path.join(target, name)
			if os.path.isdir(path) and not os.path.islink(path):
				shutil.rmtree(path)
			else:
				os.remove(path)
	for rel, data in tree.items():
		path = os.path.join(target, rel.replace("/", os.sep))
		os.makedirs(os.path.dirname(path), exist_ok=True)
		with open(path, "wb") as f:
			f.write(data)


def main():
	parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
	parser.add_argument("--commit", default="HEAD")
	parser.add_argument("--target", required=True)
	parser.add_argument("--replace", action="store_true")
	args = parser.parse_args()
	target = os.path.abspath(args.target)
	if os.path.commonpath([target, REPO]) == REPO:
		raise SystemExit("export_benchmarks: the target must be outside this repository")
	commit = git("rev-parse", "--verify", args.commit + "^{commit}").strip()
	tree = archived_tree(commit)
	generated = {
		"LICENSE": git("show", f"{commit}:Source/JaiScript/LICENSE", binary=True),
		".gitignore": GITIGNORE.encode("utf-8"),
		".gitattributes": package_export.PUBLIC_GITATTRIBUTES.encode("utf-8"),
		".gitmodules": GITMODULES.encode("utf-8"),
	}
	for name, data in generated.items():
		if name in tree:
			raise SystemExit(f"export_benchmarks: {PROJECT} tracks a root {name}; the export writes its own")
		tree[name] = data
	if any(rel == SUBMODULE or rel.startswith(SUBMODULE + "/") for rel in tree):
		raise SystemExit(f"export_benchmarks: {PROJECT} tracks {SUBMODULE}/, the submodule's path")
	exceptions = package_export.read_exceptions(os.path.join(HERE, "benchmarks_leak_exceptions.txt"))
	problems = leak_scan(tree, exceptions) + link_check(tree)
	for problem in problems:
		print(problem, file=sys.stderr)
	if problems:
		raise SystemExit(f"export_benchmarks: {len(problems)} problems; nothing written")
	write_tree(tree, target, args.replace)
	print(f"exported {len(tree)} files from {commit[:12]} to {target}")


if __name__ == "__main__":
	main()
