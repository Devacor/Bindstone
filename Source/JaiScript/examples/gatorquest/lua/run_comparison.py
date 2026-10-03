#!/usr/bin/env python3
"""Time the Gator Quest smoke simulation: JaiScript (VM, optionally the interpreter) against the Lua port
(Lua 5.4 / 5.5 and LuaJIT JIT / interpreter), serial, one process per sample, pinned to one P core,
interleaved rounds, every timed run hash-checked. The measured interval is each side's own tick loop
("ran N ticks in X ms": after world generation, player creation included, until the last tick).

  python run_comparison.py --jai jaiscript.exe --lua54 lua54.exe --lua55 lua55.exe --luajit luajit.exe
      --output DIR [--rounds 5] [--max-background-cores 1.5] [--desktop-load] [--interp]

Screening reuses the 09-29 runner's machine (examples/gloom/ports/run_comparison.py): wait for a window
below the background ceiling with no competing build/test process, reject and rerun a sample whose
background exceeded the ceiling or that overlapped a competitor. --desktop-load records the background
instead of rejecting (a desktop-load snapshot, reported as such).
"""
import argparse, importlib.util, json, os, pathlib, re, statistics, subprocess, sys, time

HERE = pathlib.Path(__file__).resolve().parent
GAME = HERE.parent
ROOT = HERE.parents[2]
SPEC = importlib.util.spec_from_file_location("gloom_comparison", ROOT / "examples/gloom/ports/run_comparison.py")
COMMON = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(COMMON)

WORKLOADS = [(2000, 2), (2000, 4), (6000, 2), (6000, 4)]
HASHES = {(2000, 2): "3237070200", (2000, 4): "1527532673", (6000, 2): "2499173128", (6000, 4): "2955240815"}
RAN = re.compile(r"^ran (\d+) ticks in (\d+) ms \((\d+) us/tick\)", re.M)
GEN = re.compile(r"\(gen (\d+) ms\)")
HASH = re.compile(r"STATE_HASH: (\d+)")

def main():
	if os.name != "nt":
		raise SystemExit("Windows only (affinity and CPU topology).")
	p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
	p.add_argument("--jai", required=True)
	p.add_argument("--lua54")
	p.add_argument("--lua55")
	p.add_argument("--luajit")
	p.add_argument("--interp", action="store_true", help="also time the JaiScript interpreter (reference only)")
	p.add_argument("--interp-rounds", type=int, default=2)
	p.add_argument("--output", required=True)
	p.add_argument("--rounds", type=int, default=5)
	p.add_argument("--seed", type=int, default=1)
	p.add_argument("--workloads", default="2000x2,2000x4,6000x2,6000x4")
	p.add_argument("--max-background-cores", type=float, default=1.5)
	p.add_argument("--max-attempts", type=int, default=20)
	p.add_argument("--desktop-load", action="store_true")
	p.add_argument("--quiet-timeout", type=float, default=7200.0, help="seconds to wait for a quiet window before each sample")
	a = p.parse_args()
	workloads = [tuple(int(v) for v in w.split("x")) for w in a.workloads.split(",")]
	out = pathlib.Path(a.output).resolve()
	out.mkdir(parents=True, exist_ok=True)
	if (out / "runs.json").exists():
		p.error("Use a fresh evidence directory.")
	jai = pathlib.Path(a.jai).resolve()
	configs = [{"key": "jai-vm", "runtime": "JaiScript VM", "cmd": [str(jai), "--no-debug", "--no-pause", "--backend=vm", "scripts/gatorquest.jai", "--", "smoke"], "extra": ["noroundtrip"]}]
	if a.lua54:
		configs.append({"key": "lua-5.4.8", "runtime": "Lua 5.4.8", "cmd": [str(pathlib.Path(a.lua54).resolve()), str(HERE / "gatorquest.lua"), "smoke"], "extra": []})
	if a.lua55:
		configs.append({"key": "lua-5.5.1", "runtime": "Lua 5.5.1", "cmd": [str(pathlib.Path(a.lua55).resolve()), str(HERE / "gatorquest.lua"), "smoke"], "extra": []})
	if a.luajit:
		j = str(pathlib.Path(a.luajit).resolve())
		configs.append({"key": "luajit-jit", "runtime": "LuaJIT 2.1 JIT", "cmd": [j, str(HERE / "luajit" / "gatorquest.lua"), "smoke"], "extra": []})
		configs.append({"key": "luajit-joff", "runtime": "LuaJIT 2.1 interpreter (-joff)", "cmd": [j, "-joff", str(HERE / "luajit" / "gatorquest.lua"), "smoke"], "extra": []})
	interp = {"key": "jai-interp", "runtime": "JaiScript interpreter (reference)", "cmd": [str(jai), "--no-debug", "--no-pause", "--backend=interp", "scripts/gatorquest.jai", "--", "smoke"], "extra": ["noroundtrip"]}
	machine = COMMON.WindowsMachine()
	binaries = {c["cmd"][0]: COMMON.file_hash(pathlib.Path(c["cmd"][0])) for c in configs}
	inputs = {str(f): COMMON.file_hash(f) for f in list(GAME.glob("scripts/*.jai")) + list(HERE.glob("*.lua")) + list(HERE.glob("luajit/*.lua"))}
	meta = {"created_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()), "rounds": a.rounds, "seed": a.seed,
		"workloads": workloads, "max_background_cores": a.max_background_cores, "desktop_load": a.desktop_load,
		"performance_cpus": machine.performance_cpus, "configs": configs + ([interp] if a.interp else []),
		"binary_sha256": binaries, "input_sha256": inputs, "runner_sha256": COMMON.file_hash(pathlib.Path(__file__)),
		"order": "Each round: every workload, every configuration in listed order; one process at a time, pinned to performance CPU 0."}
	COMMON.save(out / "metadata.json", meta)
	records = []

	BENCHMARKS = ("jai_demoreel", "demoreel_", "gloom_", "jai_gloom", "jai_doom_", "reference_host", "jai_kernel_host", "lua_kernel_host")

	def wait_window():
		# quiet mode: below the ceiling with no competitor; desktop-load mode: only never overlap another benchmark
		deadline = time.perf_counter() + a.quiet_timeout
		attempt = 0
		while True:
			others = [x for x in machine.competing_processes()] if not a.desktop_load else []
			names = subprocess.run(["tasklist", "/fo", "csv", "/nh"], capture_output=True, text=True).stdout.lower()
			benches = [b for b in BENCHMARKS if '"' + b in names]
			busy, start = machine.busy_seconds(), time.perf_counter()
			time.sleep(1)
			cores = (machine.busy_seconds() - busy) / (time.perf_counter() - start)
			if not benches and (a.desktop_load or (not others and cores <= a.max_background_cores)):
				return
			if attempt % 60 == 0:
				print(f"waiting: {cores:.2f} background cores, competitors {sorted(set(x['name'] for x in others))}, benchmarks {benches}", flush=True)
			attempt += 1
			if time.perf_counter() > deadline:
				raise RuntimeError("No measurement window within the quiet timeout")

	def run(config, ticks, bots, round_number):
		wait_window()
		cmd = config["cmd"] + ["ticks", str(ticks), "seed", str(a.seed), "bots", str(bots)] + config["extra"]
		before, start = machine.busy_seconds(), time.perf_counter()
		proc = subprocess.Popen(cmd, cwd=GAME, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
		cpus = machine.pin(proc, 1)
		competitors = {}
		try:
			while True:
				for other in machine.competing_processes():
					if other["pid"] != proc.pid:
						competitors[other["pid"]] = other
				try:
					raw, _ = proc.communicate(timeout=0.25)
					break
				except subprocess.TimeoutExpired:
					if time.perf_counter() - start > 1800:
						raise
		except BaseException:
			proc.kill()
			proc.communicate()
			raise
		wall = time.perf_counter() - start
		cpu = machine.child_seconds(proc)
		background = max(0.0, machine.busy_seconds() - before - cpu) / wall
		name = f"{len(records) + 1:04d}-{config['key']}-t{ticks}-b{bots}.log"
		(out / name).write_bytes(raw)
		text = raw.decode("utf-8", errors="replace")
		ran, gen, h = RAN.search(text), GEN.search(text), HASH.search(text)
		row = {"configuration": config["key"], "ticks": ticks, "bots": bots, "round": round_number, "log": name,
			"exit_code": proc.returncode, "affinity_cpus": cpus, "wall_seconds": wall, "cpu_seconds": cpu,
			"background_core_equivalents": background, "competitors": list(competitors.values()),
			"run_ms": int(ran.group(2)) if ran else None, "gen_ms": int(gen.group(1)) if gen else None,
			"hash": h.group(1) if h else None}
		row["hash_match"] = row["hash"] == HASHES.get((ticks, bots)) if (ticks, bots) in HASHES else row["hash"] is not None
		row["ms_per_tick"] = row["run_ms"] / ticks if row["run_ms"] is not None else None
		row["accepted"] = proc.returncode == 0 and row["hash_match"] and row["run_ms"] is not None and (
			a.desktop_load or (background <= a.max_background_cores and not competitors))
		records.append(row)
		COMMON.save(out / "runs.json", records)
		print(f"{len(records):04d} r{round_number} {config['key']:12s} t{ticks} b{bots}: {row['ms_per_tick']} ms/tick hash={row['hash_match']} "
			f"bg={background:.2f} competitors={len(competitors)} accepted={row['accepted']}", flush=True)
		if proc.returncode != 0 or not row["hash_match"]:
			raise RuntimeError(f"{config['key']} t{ticks} b{bots} failed conformance (see {name})")
		return row

	def sample(config, ticks, bots, round_number):
		for _ in range(a.max_attempts):
			if run(config, ticks, bots, round_number)["accepted"]:
				return
		raise RuntimeError("Background activity prevented a complete measurement.")

	for r in range(1, a.rounds + 1):
		for ticks, bots in workloads:
			for config in configs:
				sample(config, ticks, bots, r)
	if a.interp:
		for r in range(1, a.interp_rounds + 1):
			for ticks, bots in workloads:
				sample(interp, ticks, bots, r)
	for path, digest in {**binaries, **inputs}.items():
		if COMMON.file_hash(pathlib.Path(path)) != digest:
			raise RuntimeError("Measured input changed: " + path)
	summary = []
	for config in configs + ([interp] if a.interp else []):
		for ticks, bots in workloads:
			rows = [x for x in records if x["configuration"] == config["key"] and x["ticks"] == ticks and x["bots"] == bots and x["accepted"]]
			if not rows:
				continue
			v = [x["ms_per_tick"] for x in rows]
			bg = [x["background_core_equivalents"] for x in rows]
			summary.append({"configuration": config["key"], "runtime": config["runtime"], "ticks": ticks, "bots": bots,
				"samples": len(v), "median_ms_per_tick": statistics.median(v), "range_ms_per_tick": [min(v), max(v)],
				"median_gen_ms": statistics.median([x["gen_ms"] for x in rows]),
				"median_wall_s": statistics.median([x["wall_seconds"] for x in rows]),
				"background_cores": [min(bg), statistics.median(bg), max(bg)]})
	meta.update(complete=True, accepted=sum(x["accepted"] for x in records), rejected=sum(not x["accepted"] for x in records))
	COMMON.save(out / "metadata.json", meta)
	COMMON.save(out / "summary.json", summary)
	for s in summary:
		print(f"{s['runtime']:34s} t{s['ticks']} b{s['bots']}: {s['median_ms_per_tick']:.4f} ms/tick "
			f"[{s['range_ms_per_tick'][0]:.4f} - {s['range_ms_per_tick'][1]:.4f}] n={s['samples']} gen {s['median_gen_ms']} ms "
			f"bg {s['background_cores'][0]:.2f}/{s['background_cores'][1]:.2f}/{s['background_cores'][2]:.2f}")

if __name__ == "__main__":
	main()
