#!/usr/bin/env python3
"""Time the demoreel's heaviest scenes in the JaiScript VM, Lua 5.4, Squirrel 3.2, ChaiScript 6.1 and Python 3.

Every runtime runs the shared host's --perf workload (`--perf N --scene I --w 100 --h 40`, seed 20260705, scene time
3.0, dt 1/30) and every run's scene hash must equal the JaiScript VM's for the same command; a run that does not match
fails the session. The full protocol (VM serial and parallel, Lua, Squirrel, Python) times 60 frames after 30 warm-up
frames on the same runtime. ChaiScript is too slow for it: it times short slices spread across the same 60-frame
window, each after one warm-up frame, and its window total is projected from their mean. Serial processes are pinned to
one P core, the parallel VM row to the eight; runtimes interleave within each round, the order rotating between rounds,
with cooldown gaps; each process holds the machine's timing lock (JaiScript's tools/ci/timing_lock.sh) while it
runs and releases it after. Output: runs.json, summary.json and a markdown table under the comparison project's out/.
"""
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import statistics
import subprocess
import sys
import time

PORTS = Path(__file__).resolve().parent
COMPARISONS = PORTS.parents[1]
sys.path.insert(0, str(COMPARISONS))
import comparison_paths

LOCK = comparison_paths.TIMING_LOCK
# Git's bash runs the POSIX lock script; the bash.exe in System32 is WSL's
GIT_BASHES = (Path(r"C:\Program Files\Git\usr\bin\bash.exe"), Path(r"C:\Program Files\Git\bin\bash.exe"))
BASH = os.environ.get("DEMOREEL_BASH") or next((str(p) for p in GIT_BASHES if p.exists()), shutil.which("bash"))
SCENES = {6: "GLOBHUNTER", 9: "OSSUARY", 10: "MAELSTROM"}
SERIAL_MASK = "400"          # logical CPU 10, a P core
PARALLEL_MASK = "C03C03"     # the eight P cores: 0, 1, 10-13, 22, 23
FRAMES, WARMUP, AT = 60, 30, 3.0
SLICE_FRAMES, SLICE_WARMUP = 4, 1
SLICE_STARTS = (0, 20, 40)   # frame offsets into the 60-frame window
ROW = re.compile(r"^(\S+)\s+\|\s+(\d+)\s+\|\s+(\S+)\s+\|\s+([\d.]+)\s+\|\s+([\d.]+)\s+\|\s+([0-9a-f]{16})\s*$", re.M)
HEADER = re.compile(r"--perf: (\d+) frames per scene @ (\d+)x(\d+) from scene t=([\d.]+), seed (\d+)")


def runtimes(bin_dir):
	return [
		{"key": "jai-serial", "label": "Jai VM", "cmd": [str(bin_dir / "jai_demoreel.exe"), "--workers", "1"], "backend": "vm", "mask": SERIAL_MASK},
		{"key": "jai-parallel", "label": "Jai VM, 8 workers", "cmd": [str(bin_dir / "jai_demoreel.exe")], "backend": "vm", "mask": PARALLEL_MASK},
		{"key": "lua", "label": "Lua 5.4", "cmd": [str(bin_dir / "demoreel_lua.exe")], "backend": "lua", "mask": SERIAL_MASK},
		{"key": "squirrel", "label": "Squirrel 3.2", "cmd": [str(bin_dir / "demoreel_squirrel.exe")], "backend": "squirrel", "mask": SERIAL_MASK},
		{"key": "python", "label": f"Python {sys.version_info.major}.{sys.version_info.minor}", "cmd": [sys.executable, str(PORTS / "python" / "demoreel.py")], "backend": "python", "mask": SERIAL_MASK},
		{"key": "chai", "label": "ChaiScript 6.1", "cmd": [str(bin_dir / "demoreel_chai.exe")], "backend": "chai", "mask": SERIAL_MASK, "sampled": True},
	]


def workload(scene, frames, warmup, at):
	result = ["--perf", str(frames), "--scene", str(scene), "--w", "100", "--h", "40", "--perf-at", repr(at)]
	if warmup:
		result += ["--perf-warmup", str(warmup)]
	return result


def slice_at(start):
	return AT + start / 30.0


def parse_row(output, scene, frames, at):
	header = HEADER.search(output)
	rows = ROW.findall(output)
	if not header or int(header.group(1)) != frames or (int(header.group(2)), int(header.group(3))) != (100, 40):
		raise ValueError("missing or mismatched --perf header")
	if abs(float(header.group(4)) - at) > 0.005 or int(header.group(5)) != 20260705:
		raise ValueError("the run did not start where the workload says")
	if len(rows) != 1 or int(rows[0][1]) != scene + 1 or rows[0][2] != SCENES[scene]:
		raise ValueError("expected exactly one row for " + SCENES[scene])
	ms = float(rows[0][3])
	if ms <= 0.0:
		raise ValueError("non-positive measurement")
	return {"backend": rows[0][0], "ms_per_frame": ms, "kb_per_frame": float(rows[0][4]), "hash": rows[0][5]}


def run(config, args_tail, out, records, label, lock):
	command = ["cmd", "/c", "start", "", "/b", "/wait", "/affinity", config["mask"]] + config["cmd"] + args_tail
	if lock:
		command = [BASH, str(LOCK), "run", f"demoreel-ports: {label}", "--"] + command
	start = time.perf_counter()
	# keep MSYS from rewriting cmd's /c, /b, /wait and /affinity into paths on their way through bash
	env = dict(os.environ, MSYS_NO_PATHCONV="1", MSYS2_ARG_CONV_EXCL="*")
	done = subprocess.run(command, cwd=out, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=7200)
	name = f"{len(records) + 1:04d}-{label.replace(' ', '_')}.log"
	(out / name).write_bytes(done.stdout)
	return done.returncode, done.stdout.decode("utf-8", errors="replace"), time.perf_counter() - start, name


def measure(config, scene, frames, warmup, at, expected, out, records, phase, lock, round_number=0):
	label = f"{config['key']} s{scene} f{frames} at{at:.3f} {phase}"
	code, text, wall, log = run(config, workload(scene, frames, warmup, at), out, records, label, lock)
	row = {"configuration": config["key"], "scene": scene, "frames": frames, "warmup": warmup, "at": at, "phase": phase,
		"round": round_number, "exit_code": code, "wall_seconds": wall, "log": log}
	try:
		row.update(parse_row(text, scene, frames, at))
	except ValueError as error:
		row.update(error=str(error), hash=None, ms_per_frame=None)
	row["hash_match"] = code == 0 and row["hash"] is not None and row["backend"] == config["backend"] and (expected is None or row["hash"] == expected)
	records.append(row)
	(out / "runs.json").write_text(json.dumps(records, indent=1))
	print(f"{len(records):04d} {label}: {row['ms_per_frame']} ms/frame hash={row['hash']} match={row['hash_match']}", flush=True)
	if not row["hash_match"]:
		raise RuntimeError(f"{label}: hash {row['hash']} does not match the VM's {expected} (log {log})")
	return row


def summarize(configs, records, scenes):
	result = []
	for scene in scenes:
		for config in configs:
			timed = [r for r in records if r["configuration"] == config["key"] and r["scene"] == scene and r["phase"] == "timed"]
			if not timed:
				continue
			row = {"scene": SCENES[scene], "configuration": config["key"], "label": config["label"]}
			if config.get("sampled"):
				slices = {}
				for r in timed:
					slices.setdefault(r["at"], []).append(r["ms_per_frame"])
				medians = [statistics.median(v) for v in slices.values()]
				row.update(ms_per_frame=statistics.mean(medians), slice_medians_ms=medians,
					range_ms=[min(min(v) for v in slices.values()), max(max(v) for v in slices.values())],
					sampled_frames=SLICE_FRAMES * len(slices), runs_per_slice=min(len(v) for v in slices.values()),
					projected_window_ms=statistics.mean(medians) * FRAMES)
			else:
				values = [r["ms_per_frame"] for r in timed]
				row.update(ms_per_frame=statistics.median(values), range_ms=[min(values), max(values)], runs=len(values),
					window_ms=statistics.median(values) * FRAMES)
			result.append(row)
	return result


def table(summary, scenes):
	by = {(r["scene"], r["configuration"]): r for r in summary}
	lines = ["| scene | Jai VM | Jai VM, 8 workers | Lua 5.4 | Squirrel 3.2 | ChaiScript 6.1 | Python | VM / Lua (8 workers, serial) |",
		"|---|---|---|---|---|---|---|---|"]
	for scene in scenes:
		name = SCENES[scene]
		def cell(key):
			r = by.get((name, key))
			if not r:
				return "n/a"
			text = f"{r['ms_per_frame']:.2f} ({r['range_ms'][0]:.2f}-{r['range_ms'][1]:.2f})"
			return text + (f" from {r['sampled_frames']} frames" if "sampled_frames" in r else "")
		ratio = "n/a"
		if (name, "lua") in by and (name, "jai-serial") in by:
			lua = by[(name, "lua")]["ms_per_frame"]
			ratio = f"{by[(name, 'jai-parallel')]['ms_per_frame'] / lua:.2f}, {by[(name, 'jai-serial')]['ms_per_frame'] / lua:.2f}" if (name, "jai-parallel") in by else f"{by[(name, 'jai-serial')]['ms_per_frame'] / lua:.2f}"
		lines.append(f"| {name} | {cell('jai-serial')} | {cell('jai-parallel')} | {cell('lua')} | {cell('squirrel')} | {cell('chai')} | {cell('python')} | {ratio} |")
	return "\n".join(lines)


def main():
	if os.name != "nt":
		raise SystemExit("This runner pins processes with Windows affinity masks.")
	parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
	parser.add_argument("--bin", type=Path, default=COMPARISONS / "out" / "release" / "bin", help="the comparison build's bin folder")
	parser.add_argument("--output", type=Path, required=True, help="a fresh evidence folder under the comparison project's out/")
	parser.add_argument("--build", action="store_true", help="build the four executables first (run inside a VsDevCmd x64 prompt)")
	parser.add_argument("--rounds", type=int, default=5)
	parser.add_argument("--chai-rounds", type=int, default=3)
	parser.add_argument("--runtimes", default="jai-serial,jai-parallel,lua,squirrel,python,chai")
	parser.add_argument("--scenes", default="9,10,6")
	parser.add_argument("--cooldown", type=float, default=4.0, help="seconds between processes")
	parser.add_argument("--round-cooldown", type=float, default=20.0, help="seconds between rounds")
	parser.add_argument("--conformance-only", action="store_true")
	parser.add_argument("--no-lock", action="store_true", help="do not take the timing lock (conformance checks only)")
	args = parser.parse_args()
	out = args.output.resolve()
	out.relative_to((COMPARISONS / "out").resolve())
	out.mkdir(parents=True, exist_ok=True)
	if (out / "runs.json").exists():
		parser.error("use a fresh evidence folder")
	if args.rounds < 5 or args.chai_rounds < 3:
		parser.error("the protocol takes at least five rounds (three for the sampled ChaiScript slices)")
	if args.build:
		subprocess.run(["cmake", "--build", str(args.bin.parent), "--target", "jai_demoreel", "demoreel_lua", "demoreel_squirrel", "demoreel_chai"], check=True)
	scenes = [int(s) for s in args.scenes.split(",")]
	wanted = args.runtimes.split(",")
	configs = [c for c in runtimes(args.bin.resolve()) if c["key"] in wanted]
	reference = next(c for c in runtimes(args.bin.resolve()) if c["key"] == "jai-serial")
	lock = not args.no_lock
	records = []
	metadata = {"created_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()), "frames": FRAMES, "warmup": WARMUP, "at": AT,
		"slice_frames": SLICE_FRAMES, "slice_warmup": SLICE_WARMUP, "slice_starts": SLICE_STARTS, "rounds": args.rounds,
		"chai_rounds": args.chai_rounds, "configs": configs, "scenes": {s: SCENES[s] for s in scenes},
		"masks": {"serial": SERIAL_MASK, "parallel": PARALLEL_MASK}, "python": sys.version}
	(out / "metadata.json").write_text(json.dumps(metadata, indent=1))
	# The VM is the oracle: one unwarmed reference per workload shape; warm-up never changes a scene's hash.
	expected = {}
	for scene in scenes:
		expected[(scene, FRAMES, AT)] = measure(reference, scene, FRAMES, 0, AT, None, out, records, "reference", lock)["hash"]
		if any(c.get("sampled") for c in configs):
			for start in SLICE_STARTS:
				at = slice_at(start)
				expected[(scene, SLICE_FRAMES, at)] = measure(reference, scene, SLICE_FRAMES, 0, at, None, out, records, "reference", lock)["hash"]
	for scene in scenes:
		for config in configs:
			if config.get("sampled"):
				for start in SLICE_STARTS:
					at = slice_at(start)
					measure(config, scene, SLICE_FRAMES, 0, at, expected[(scene, SLICE_FRAMES, at)], out, records, "gate", lock)
			else:
				measure(config, scene, FRAMES, 0, AT, expected[(scene, FRAMES, AT)], out, records, "gate", lock)
	if not args.conformance_only:
		full = [c for c in configs if not c.get("sampled")]
		for round_number in range(1, args.rounds + 1):
			shift = (round_number - 1) % max(1, len(full))
			order = full[shift:] + full[:shift]
			for scene in scenes:
				for config in order:
					measure(config, scene, FRAMES, WARMUP, AT, expected[(scene, FRAMES, AT)], out, records, "timed", lock, round_number)
					time.sleep(args.cooldown)
			time.sleep(args.round_cooldown)
		for config in [c for c in configs if c.get("sampled")]:
			for round_number in range(1, args.chai_rounds + 1):
				for scene in scenes:
					for start in SLICE_STARTS:
						at = slice_at(start)
						measure(config, scene, SLICE_FRAMES, SLICE_WARMUP, at, expected[(scene, SLICE_FRAMES, at)], out, records, "timed", lock, round_number)
						time.sleep(args.cooldown)
	summary = summarize(configs, records, scenes)
	(out / "summary.json").write_text(json.dumps(summary, indent=1))
	(out / "table.md").write_text(table(summary, scenes) + "\n")
	print(table(summary, scenes))


if __name__ == "__main__":
	main()
