#!/usr/bin/env python3
"""Compare frozen OSSUARY revisions and Lua/Squirrel with warmed, pinned, interleaved padding controls."""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import re
import statistics
import subprocess
import time

ROOT = Path(__file__).resolve().parents[3]
SPEC = importlib.util.spec_from_file_location("gloom_comparison", ROOT / "examples/gloom/ports/run_comparison.py")
COMMON = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(COMMON)
ROW = re.compile(r"^(\S+)\s+\|\s+16\s+\|\s+OSSUARY\s+\|\s+([\d.]+)\s+\|\s+([\d.]+)\s+\|\s+([0-9a-f]{16})\s*$", re.M)

def parse_scene(output, frames):
	header = re.search(r"--perf:\s+(\d+) frames per scene @ (\d+)x(\d+) from scene t=([\d.]+), seed (\d+), workers (-?\d+)", output)
	rows = ROW.findall(output)
	if not header or tuple(map(int, header.group(1, 2, 3))) != (frames, 100, 40) or float(header.group(4)) != 3.0 or int(header.group(5)) != 20260705 or len(rows) != 1:
		raise ValueError("Missing or mismatched OSSUARY workload row")
	if float(rows[0][1]) <= 0:
		raise ValueError("Non-positive OSSUARY measurement")
	return {"backend":rows[0][0], "ms_per_frame":float(rows[0][1]), "kb_per_frame":float(rows[0][2]), "hash":rows[0][3]}

def summarize(config, records):
	values = {layout:[r["ms_per_frame"] for r in records if r["configuration"] == config["key"] and r["layout"] == layout and r["phase"] == "timed" and r["accepted"]] for layout in ("normal", "padding")}
	row = dict(config)
	if values["normal"] and len(values["normal"]) == len(values["padding"]):
		a, b = values["normal"], values["padding"]
		deltas = [100 * (y / x - 1) for x, y in zip(a, b)]
		row.update(normal_median_ms=statistics.median(a), padding_median_ms=statistics.median(b),
			normal_range_ms=[min(a), max(a)], padding_range_ms=[min(b), max(b)],
			padding_paired_band_percent=[min(deltas), max(deltas)], runs_per_layout=len(a))
	return row

def main():
	if os.name != "nt":
		raise SystemExit("This runner records Windows CPU topology and process affinity.")
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("--baseline", type=Path, required=True)
	parser.add_argument("--candidate", type=Path, required=True)
	parser.add_argument("--scenes", type=Path, required=True)
	parser.add_argument("--natural-scenes", type=Path, required=True)
	parser.add_argument("--lua-scenes", type=Path, required=True)
	parser.add_argument("--squirrel-scenes", type=Path, required=True)
	parser.add_argument("--output", type=Path, required=True)
	parser.add_argument("--rounds", type=int, default=3)
	parser.add_argument("--frames", type=int, default=5)
	parser.add_argument("--warmup", type=int, default=5)
	parser.add_argument("--max-background-cores", type=float, default=0.20)
	parser.add_argument("--max-attempts", type=int, default=20)
	parser.add_argument("--conformance-only", action="store_true")
	args = parser.parse_args()
	if args.rounds < 3 or args.frames < 1 or args.warmup < args.frames:
		parser.error("Use at least three rounds and a full workload of warm-up frames.")
	out = args.output.resolve()
	out.relative_to((ROOT / "out/astra").resolve())
	out.mkdir(parents=True, exist_ok=True)
	if (out / "runs.json").exists():
		parser.error("Use a fresh evidence directory.")
	builds = {"baseline":args.baseline.resolve(), "candidate":args.candidate.resolve()}
	scene_dirs = {"shaped":args.scenes.resolve(), "natural":args.natural_scenes.resolve(),
		"lua":args.lua_scenes.resolve(), "squirrel":args.squirrel_scenes.resolve()}
	configs = []
	for shape in ("shaped", "natural"):
		for workers in (1, 6):
			for variant in builds:
				configs.append({"key":f"jai-{variant}-{shape}-w{workers}", "runtime":"JaiScript VM", "variant":variant,
					"shape":shape, "target":"jai_demoreel", "backend":"vm", "workers":workers, "cores":workers})
	configs[2:2] = [
		{"key":"lua-5.4.8", "runtime":"Lua 5.4.8", "variant":"baseline", "shape":"lua", "target":"demoreel_lua", "backend":"lua", "workers":1, "cores":1},
		{"key":"squirrel-3.2", "runtime":"Squirrel 3.2", "variant":"baseline", "shape":"squirrel", "target":"demoreel_squirrel", "backend":"squirrel", "workers":1, "cores":1}]
	machine = COMMON.WindowsMachine()
	binaries = {}
	for config in configs:
		for suffix in ("", "_padding"):
			file = builds[config["variant"]] / "bin" / (config["target"] + suffix + ".exe")
			binaries[str(file)] = COMMON.file_hash(file)
	inputs = {str(p):COMMON.file_hash(p) for folder in scene_dirs.values() for p in folder.rglob("*") if p.suffix in (".jai", ".lua", ".nut")}
	metadata = {"created_utc":time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()), "frames":args.frames, "warmup":args.warmup,
		"rounds":args.rounds, "scene":15, "scene_name":"OSSUARY", "dimensions":[100,40], "scene_time":3.0, "seed":20260705,
		"max_background_cores":args.max_background_cores, "cpu_sets":machine.cpu_sets, "performance_cpus":machine.performance_cpus,
		"builds":{k:str(v) for k,v in builds.items()}, "scene_dirs":{k:str(v) for k,v in scene_dirs.items()},
		"binary_sha256":binaries, "workload_sha256":inputs, "runner_sha256":COMMON.file_hash(Path(__file__)),
		"configs":configs, "order":"Each round: twice [normal configurations in listed order, padding configurations in listed order].",
		"padding":"40 retained volatile statements in the cold help function; a separately linked binary per runtime",
		"measurement":"Full scene warmup, reboot/jump in the same runtime; measured update/render/presentation/frame hashing."}
	COMMON.save(out / "metadata.json", metadata)
	records = []
	def run(config, layout, phase, round_number=0, expected=None):
		machine.wait_for_quiet(args.max_background_cores)
		exe = builds[config["variant"]] / "bin" / (config["target"] + ("_padding" if layout == "padding" else "") + ".exe")
		command = [str(exe), "--perf",str(args.frames), "--perf-warmup",str(args.warmup), "--scene","15",
			"--backend",config["backend"], "--workers",str(config["workers"]), "--seed","20260705", "--w","100","--h","40",
			"--scenes",str(scene_dirs[config["shape"]])]
		before, start = machine.busy_seconds(), time.perf_counter()
		process = subprocess.Popen(command, cwd=out, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
		cpus = machine.pin(process, config["cores"])
		competitors = {}
		try:
			while True:
				for other in machine.competing_processes():
					if other["pid"] != process.pid:
						competitors[other["pid"]] = other
				try:
					raw, _ = process.communicate(timeout=0.25)
					break
				except subprocess.TimeoutExpired:
					if time.perf_counter() - start > 600:
						raise
		except BaseException:
			process.kill()
			process.communicate()
			raise
		wall = time.perf_counter() - start
		cpu = machine.child_seconds(process)
		background = max(0.0, machine.busy_seconds() - before - cpu) / wall
		name = f"{len(records)+1:03d}-{config['key']}-{layout}-{phase}.log"
		(out / name).write_bytes(raw)
		output = raw.decode("utf-8", errors="replace")
		try:
			parsed = parse_scene(output, args.frames)
		except ValueError as error:
			parsed = {"error":str(error), "hash":None, "ms_per_frame":None}
		matches = parsed["hash"] is not None and parsed.get("backend") == config["backend"] and (expected is None or parsed["hash"] == expected)
		row = dict(parsed, configuration=config["key"], layout=layout, phase=phase, round=round_number,
			command=command, log=name, exit_code=process.returncode, affinity_cpus=cpus, wall_seconds=wall, cpu_seconds=cpu,
			background_core_equivalents=background, competitors=list(competitors.values()) + machine.competing_processes(),
			hash_match=matches)
		row["accepted"] = process.returncode == 0 and matches and background <= args.max_background_cores and not row["competitors"]
		records.append(row)
		COMMON.save(out / "runs.json", records)
		print(f"{len(records):03d} {config['key']} {layout} {phase}: {row['ms_per_frame']} ms/frame hash={matches} background={background:.3f} accepted={row['accepted']}", flush=True)
		return row
	# Interpreter supplies the reference under the identical host/workload, not a performance point.
	oracle = dict(configs[0], key="jai-interpreter-reference", backend="interpreter")
	ref = run(oracle, "normal", "reference")
	if ref["exit_code"] or not ref["hash_match"]:
		raise RuntimeError("Interpreter OSSUARY reference failed.")
	expected = ref["hash"]
	metadata["expected_hash"] = expected
	valid = []
	for config in configs:
		gates = [run(config, layout, "gate", expected=expected) for layout in ("normal", "padding")]
		if all(r["exit_code"] == 0 and r["hash_match"] for r in gates):
			valid.append(config)
		elif config["runtime"] == "JaiScript VM":
			raise RuntimeError("JaiScript OSSUARY conformance failed.")
		else:
			print(config["key"] + " is nonconforming and will not be timed.", flush=True)
	if not args.conformance_only:
		for round_number in range(1, args.rounds + 1):
			for repeat in range(2):
				for layout in ("normal", "padding"):
					for config in valid:
						for attempt in range(args.max_attempts):
							row = run(config, layout, "timed", round_number, expected)
							if row["exit_code"] or not row["hash_match"]:
								raise RuntimeError("Conformance changed during timing.")
							if row["accepted"]:
								break
						else:
							raise RuntimeError("Background activity prevented a complete measurement.")
	for path, digest in {**binaries, **inputs}.items():
		if COMMON.file_hash(Path(path)) != digest:
			raise RuntimeError("Measured input changed: " + path)
	summary = []
	for config in configs:
		row = summarize(config, records)
		row["conforming"] = config in valid
		if not args.conformance_only and config in valid and row.get("runs_per_layout") != 2 * args.rounds:
			raise RuntimeError("Incomplete timing set")
		summary.append(row)
	metadata.update(complete=True, accepted_timings=sum(r["phase"] == "timed" and r["accepted"] for r in records),
		rejected_timings=sum(r["phase"] == "timed" and not r["accepted"] for r in records))
	COMMON.save(out / "metadata.json", metadata)
	COMMON.save(out / "summary.json", summary)
	print(json.dumps(summary, indent=2))

if __name__ == "__main__":
	main()
