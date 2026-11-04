#!/usr/bin/env python3
"""Runs the benchmark apps (Richards, DeltaBlue, the bytecode VM, the spatial hash) in every language and checks hash parity.

Each sample is one process; it prints `result <hash>` and `time_ms <ms>` (its own clock around the workload, so
start-up and compilation stay out). Samples are interleaved round robin across implementations, --rounds per cell; a
cell reports the median and the spread (half the sample range as a share of the median). Every sample's hash must
equal the app's reference hash (the first implementation's), or the run fails. The whole run holds the machine-wide
timing lock (JaiScript's tools/ci/timing_lock.sh) unless --no-lock. Writes out/apps/RESULTS_<date>.md.

    python run.py [--rounds 5] [--apps richards,deltablue,bytecode,spatial] [--impls jai_vm,jai_interp,lua,squirrel,python]
                  [--bin <dir holding jaiscript.exe, apps_lua.exe, apps_sq.exe>] [--python <exe>] [--no-lock] [--out FILE]
"""
import argparse
import datetime
import os
import platform
import re
import statistics
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
TIMING_LOCK = os.path.normpath(os.path.join(HERE, "..", "..", "JaiScript", "tools", "ci", "timing_lock.sh"))
DEFAULT_BIN = os.path.normpath(os.path.join(HERE, "..", "out", "release", "bin"))

APPS = ["richards", "deltablue", "bytecode", "spatial"]
IMPLS = ["jai_vm", "jai_interp", "lua", "squirrel", "python"]
TITLES = {"jai_vm": "JaiScript VM", "jai_interp": "JaiScript interp", "lua": "Lua 5.4", "squirrel": "Squirrel 3.2",
	"python": "CPython"}
EXE = ".exe" if os.name == "nt" else ""


def command(impl, app, args):
	folder = os.path.join(HERE, app)
	if impl.startswith("jai_"):
		backend = "interp" if impl == "jai_interp" else "vm"
		return [os.path.join(args.bin, "jaiscript" + EXE), "--no-pause", f"--backend={backend}",
			os.path.join(folder, app + ".jai")]
	if impl == "lua":
		return [os.path.join(args.bin, "apps_lua" + EXE), os.path.join(folder, app + ".lua")]
	if impl == "squirrel":
		return [os.path.join(args.bin, "apps_sq" + EXE), os.path.join(folder, app + ".nut")]
	return [args.python, "-I", os.path.join(folder, app + ".py")]


def sample(impl, app, args):
	process = subprocess.run(command(impl, app, args), cwd=HERE, capture_output=True, text=True, timeout=1800)
	result = re.search(r"^result (\d+)\s*$", process.stdout, re.M)
	elapsed = re.search(r"^time_ms ([0-9.]+)\s*$", process.stdout, re.M)
	if process.returncode != 0 or not result or not elapsed:
		lines = (process.stderr or process.stdout).strip().splitlines()
		raise SystemExit(f"{app} {impl}: failed (exit {process.returncode}): {lines[0] if lines else 'no output'}")
	return result.group(1), float(elapsed.group(1))


def version(args):
	result = {}
	result["python"] = subprocess.run([args.python, "-c", "import platform; print(platform.python_version())"],
		capture_output=True, text=True).stdout.strip()
	return result


def machine():
	try:
		cpu = subprocess.run(["powershell", "-NoProfile", "-Command", "(Get-CimInstance Win32_Processor).Name"],
			capture_output=True, text=True).stdout.strip()
		return f"{cpu}, {platform.system()} {platform.release()}"
	except OSError:
		return platform.platform()


def posix_shell():
	"""Git's bash on Windows (a bare `bash` there can resolve to WSL's launcher); `bash` elsewhere; JAI_BASH overrides."""
	if os.environ.get("JAI_BASH"):
		return os.environ["JAI_BASH"]
	if os.name == "nt":
		for root in (os.environ.get("ProgramFiles", r"C:\Program Files"), os.environ.get("ProgramW6432", "")):
			candidate = os.path.join(root, "Git", "bin", "bash.exe")
			if root and os.path.exists(candidate):
				return candidate
	return "bash"


def fmt(ms):
	return f"{ms:.0f}" if ms >= 100 else f"{ms:.1f}"


def main():
	parser = argparse.ArgumentParser()
	parser.add_argument("--rounds", type=int, default=5)
	parser.add_argument("--apps", default=",".join(APPS))
	parser.add_argument("--impls", default=",".join(IMPLS))
	parser.add_argument("--bin", default=os.environ.get("APPS_BIN", DEFAULT_BIN))
	parser.add_argument("--python", default=sys.executable)
	parser.add_argument("--no-lock", action="store_true")
	parser.add_argument("--locked", action="store_true", help=argparse.SUPPRESS)
	parser.add_argument("--out", default="")
	args = parser.parse_args()

	if not args.no_lock and not args.locked:
		forwarded = [a for a in sys.argv[1:]] + ["--locked"]
		script = os.path.abspath(__file__).replace("\\", "/")
		lock = TIMING_LOCK.replace("\\", "/")
		python = sys.executable.replace("\\", "/")
		sys.exit(subprocess.run([posix_shell(), lock, "run", "apps: run.py", "--", python, script] + forwarded).returncode)

	apps = args.apps.split(",")
	impls = args.impls.split(",")
	results = {}
	for app in apps:
		times = {impl: [] for impl in impls}
		hashes = {impl: set() for impl in impls}
		for round_index in range(args.rounds):
			for impl in impls:
				digest, ms = sample(impl, app, args)
				times[impl].append(ms)
				hashes[impl].add(digest)
		reference = hashes[impls[0]]
		for impl in impls:
			if len(hashes[impl]) != 1 or hashes[impl] != reference:
				raise SystemExit(f"{app}: hash mismatch: " + ", ".join(f"{i}={sorted(hashes[i])}" for i in impls))
		row = {}
		for impl in impls:
			xs = sorted(times[impl])
			median = statistics.median(xs)
			row[impl] = (median, (xs[-1] - xs[0]) / 2 / median * 100)
		results[app] = (next(iter(reference)), row)
		print(f"{app:10s} hash {results[app][0]}  " + "  ".join(f"{impl}={fmt(row[impl][0])}" for impl in impls), flush=True)
	write_report(results, apps, impls, args)


def write_report(results, apps, impls, args):
	date = datetime.date.today().isoformat()
	out = args.out or os.path.normpath(os.path.join(HERE, "..", "out", "apps", f"RESULTS_{date}.md"))
	os.makedirs(os.path.dirname(out), exist_ok=True)
	versions = version(args) if "python" in impls else {}
	lines = [f"# Benchmark apps results {date}", "",
		f"Machine: {machine()}. CPython {versions.get('python', '-')}. Median of {args.rounds} processes per cell, "
		"interleaved round robin; each process times its own workload (start-up and compilation excluded). Times in "
		"ms, lower is better; `±` is half the sample range as a share of the median. Every sample's hash matched.", ""]
	head = ["app", "hash"] + [TITLES.get(i, i) for i in impls]
	if "jai_vm" in impls:
		head += [f"JaiScript VM / {TITLES.get(i, i)}" for i in impls if i not in ("jai_vm", "jai_interp")]
	lines.append("| " + " | ".join(head) + " |")
	lines.append("| --- | --- |" + " ---: |" * (len(head) - 2))
	for app in apps:
		digest, row = results[app]
		cells = [app, digest] + [f"{fmt(row[i][0])} ±{row[i][1]:.0f}%" for i in impls]
		if "jai_vm" in impls:
			cells += [f"{row['jai_vm'][0] / row[i][0]:.2f}" for i in impls if i not in ("jai_vm", "jai_interp")]
		lines.append("| " + " | ".join(cells) + " |")
	with open(out, "w", encoding="utf-8", newline="\n") as handle:
		handle.write("\n".join(lines) + "\n")
	print(f"wrote {out}")


if __name__ == "__main__":
	main()
