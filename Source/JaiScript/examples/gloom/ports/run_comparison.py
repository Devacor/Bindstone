#!/usr/bin/env python3
"""Run pinned GLOOM comparisons serially: 600 warm-up ticks, then the original 600 ticks."""
import argparse
import ctypes
from ctypes import wintypes
import hashlib
import json
import os
import pathlib
import re
import statistics
import struct
import subprocess
import time

CONFIGURATIONS = (
	("JaiScript VM serial", "jai_gloom", "vm", 0, 1),
	("JaiScript VM 8 workers", "jai_gloom", "vm", 8, 8),
	("Lua 5.4.8", "gloom_lua", "lua", 0, 1),
	("Lua 5.5.1", "gloom_lua55", "lua55", 0, 1),
	("LuaJIT 2.1 JIT", "gloom_luajit", "luajit-jit", 0, 1),
)
HASHES = ((300, "7fecc09815bb64e0", 3580805725), (600, "dfd969ed5a336dca", 1503537018))
ROW = re.compile(r"^(\S+)\s+\|\s+([\d.]+)\s+\|\s+([\d.]+)\s+\|\s+([0-9a-f]{16})\s+\|\s+(\d+)\s*$", re.M)

def file_hash(path):
	return hashlib.sha256(path.read_bytes()).hexdigest()

class WindowsMachine:
	def __init__(self):
		self.kernel = ctypes.WinDLL("kernel32", use_last_error=True)
		self.kernel.SetProcessAffinityMask.argtypes = [wintypes.HANDLE, ctypes.c_size_t]
		self.kernel.SetProcessAffinityMask.restype = wintypes.BOOL
		self.kernel.GetProcessTimes.argtypes = [wintypes.HANDLE] + [ctypes.POINTER(wintypes.FILETIME)] * 4
		self.kernel.GetSystemTimes.argtypes = [ctypes.POINTER(wintypes.FILETIME)] * 3
		self.kernel.GetSystemCpuSetInformation.argtypes = [
			ctypes.c_void_p, wintypes.ULONG, ctypes.POINTER(wintypes.ULONG), wintypes.HANDLE, wintypes.ULONG]
		self.cpu_sets = self.read_cpu_sets()
		classes = [x["efficiency_class"] for x in self.cpu_sets]
		best = max(classes)
		self.performance_cpus = [x["logical_cpu"] for x in self.cpu_sets if x["efficiency_class"] == best]
		if len(self.performance_cpus) < 8:
			raise RuntimeError("Eight CPUs in the fastest efficiency class are required")

	def read_cpu_sets(self):
		length = wintypes.ULONG()
		self.kernel.GetSystemCpuSetInformation(None, 0, ctypes.byref(length), None, 0)
		buffer = ctypes.create_string_buffer(length.value)
		if not self.kernel.GetSystemCpuSetInformation(buffer, length, ctypes.byref(length), None, 0):
			raise ctypes.WinError(ctypes.get_last_error())
		raw, offset, result = buffer.raw, 0, []
		while offset < length.value:
			size, kind = struct.unpack_from("<II", raw, offset)
			if kind == 0:
				group = struct.unpack_from("<H", raw, offset + 12)[0]
				if group != 0:
					raise RuntimeError("This runner requires a single Windows processor group")
				result.append({"logical_cpu": raw[offset+14], "core": raw[offset+15],
					"efficiency_class": raw[offset+18]})
			offset += size
		return result

	@staticmethod
	def seconds(value):
		return ((value.dwHighDateTime << 32) | value.dwLowDateTime) / 10000000.0

	def busy_seconds(self):
		idle, kernel, user = wintypes.FILETIME(), wintypes.FILETIME(), wintypes.FILETIME()
		if not self.kernel.GetSystemTimes(ctypes.byref(idle), ctypes.byref(kernel), ctypes.byref(user)):
			raise ctypes.WinError(ctypes.get_last_error())
		return self.seconds(kernel) + self.seconds(user) - self.seconds(idle)

	def child_seconds(self, process):
		times = [wintypes.FILETIME() for _ in range(4)]
		if not self.kernel.GetProcessTimes(int(process._handle), *(ctypes.byref(t) for t in times)):
			raise ctypes.WinError(ctypes.get_last_error())
		return self.seconds(times[2]) + self.seconds(times[3])

	def competing_processes(self):
		class Entry(ctypes.Structure):
			_fields_ = [("size",wintypes.DWORD),("usage",wintypes.DWORD),("pid",wintypes.DWORD),
				("heap",ctypes.c_size_t),("module",wintypes.DWORD),("threads",wintypes.DWORD),
				("parent",wintypes.DWORD),("priority",wintypes.LONG),("flags",wintypes.DWORD),
				("name",wintypes.WCHAR*260)]
		self.kernel.CreateToolhelp32Snapshot.argtypes = [wintypes.DWORD,wintypes.DWORD]
		self.kernel.CreateToolhelp32Snapshot.restype = wintypes.HANDLE
		self.kernel.Process32FirstW.argtypes = [wintypes.HANDLE,ctypes.POINTER(Entry)]
		self.kernel.Process32NextW.argtypes = [wintypes.HANDLE,ctypes.POINTER(Entry)]
		self.kernel.CloseHandle.argtypes = [wintypes.HANDLE]
		handle = self.kernel.CreateToolhelp32Snapshot(2,0)
		if handle == ctypes.c_void_p(-1).value:
			raise ctypes.WinError(ctypes.get_last_error())
		entry, result = Entry(), []
		entry.size = ctypes.sizeof(entry)
		try:
			more = self.kernel.Process32FirstW(handle,ctypes.byref(entry))
			while more:
				name = entry.name.lower()
				if name in ("cl.exe","link.exe","ninja.exe","msbuild.exe","jaiscript_tests.exe") or name.startswith(("jai_gloom","gloom_lua")):
					result.append({"name":name,"pid":entry.pid})
				more = self.kernel.Process32NextW(handle,ctypes.byref(entry))
		finally:
			self.kernel.CloseHandle(handle)
		return result

	def wait_for_quiet(self, limit):
		for attempt in range(600):
			competing = self.competing_processes()
			busy, start = self.busy_seconds(), time.perf_counter()
			time.sleep(1)
			cores = (self.busy_seconds()-busy)/(time.perf_counter()-start)
			if not competing and cores <= limit:
				return
			if attempt % 15 == 0:
				print(f"Waiting for quiet: {cores:.3f} background cores; competitors={competing}",flush=True)
		raise RuntimeError("No quiet measurement window within ten minutes")

	def pin(self, process, count):
		cpus = self.performance_cpus[:count]
		mask = sum(1 << cpu for cpu in cpus)
		if not self.kernel.SetProcessAffinityMask(int(process._handle), mask):
			raise ctypes.WinError(ctypes.get_last_error())
		return cpus

def save(path, data):
	path.write_text(json.dumps(data, indent=2) + "\n")

def source_fingerprint(root):
	files = sorted((root/"include").rglob("*.hpp"))
	files += sorted((root/"source/implementation").rglob("*.cpp"))
	files += sorted((root/"examples/gloom").glob("*.cpp"))
	files += sorted((root/"examples/gloom").glob("*.hpp"))
	files += sorted((root/"examples/gloom/scripts").glob("*.jai"))
	files += sorted((root/"examples/gloom/ports").rglob("*.lua"))
	entries = {str(p.relative_to(root)): file_hash(p) for p in files}
	return entries

def main():
	if os.name != "nt":
		raise SystemExit("This measurement runner records Windows CPU topology and affinity.")
	root = pathlib.Path(__file__).resolve().parents[3]
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("--build", type=pathlib.Path, default=root/"out/astra/release")
	parser.add_argument("--output", type=pathlib.Path, default=root/"out/astra/comparison")
	parser.add_argument("--rounds", type=int, default=3, help="Each round is A B A B; at least three.")
	parser.add_argument("--max-background-cores", type=float, default=0.20)
	parser.add_argument("--conformance-only", action="store_true")
	args = parser.parse_args()
	if args.rounds < 3:
		parser.error("At least three ABAB rounds are required.")
	out = args.output.resolve()
	out.relative_to((root/"out").resolve())
	out.mkdir(parents=True, exist_ok=True)
	machine = WindowsMachine()
	sources = source_fingerprint(root)
	metadata = {
		"created_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
		"head": subprocess.check_output(["git","rev-parse","HEAD"], cwd=root, text=True).strip(),
		"build": str(args.build.resolve()), "cpu_sets": machine.cpu_sets,
		"performance_cpus": machine.performance_cpus, "source_sha256": sources,
		"warmup_ticks": 600, "ticks": 600, "seed": 666, "size": [100,40], "pix": "quad",
		"rounds": args.rounds, "order": "ABAB per configuration per round; one child process at a time",
		"max_background_cores": args.max_background_cores,
		"padding": "40 volatile statements in the shared host's cold --help function",
	}
	binaries = {}
	for _, target, _, _, _ in CONFIGURATIONS:
		for suffix in ("", "_padding"):
			exe = args.build.resolve()/"bin"/(target+suffix+".exe")
			binaries[str(exe)] = file_hash(exe)
	metadata["binary_sha256"] = binaries
	save(out/"metadata.json", metadata)
	records, sequence = [], 0

	def run(config, variant, ticks, warmup, phase, round_number=0):
		nonlocal sequence
		name, target, backend, workers, cores = config
		exe = args.build.resolve()/"bin"/(target+("_padding" if variant == "B" else "")+".exe")
		command = [str(exe), "--smoke", "--backend", backend, "--workers", str(workers),
			"--ticks", str(ticks), "--warmup", str(warmup), "--seed", "666", "--pix", "quad"]
		if phase == "timed":
			machine.wait_for_quiet(args.max_background_cores)
		sequence += 1
		filename = f"{sequence:03d}-{target}-w{workers}-{phase}-{variant}.log"
		busy_start, wall_start = machine.busy_seconds(), time.perf_counter()
		process = subprocess.Popen(command, cwd=out, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
		cpus = machine.pin(process, cores)
		try:
			raw, _ = process.communicate(timeout=180)
		except subprocess.TimeoutExpired:
			process.kill()
			raw, _ = process.communicate()
			(out/filename).write_bytes(raw)
			raise RuntimeError("Benchmark timed out: " + filename)
		wall = time.perf_counter() - wall_start
		cpu = machine.child_seconds(process)
		background = max(0.0, machine.busy_seconds() - busy_start - cpu) / wall
		(out/filename).write_bytes(raw)
		text = raw.decode("utf-8", errors="replace")
		rows = ROW.findall(text)
		expected = next(x for x in HASHES if x[0] == ticks)
		hash_match = len(rows) == 1 and rows[0][3] == expected[1] and int(rows[0][4]) == expected[2]
		record = {"sequence": sequence, "configuration": name, "variant": variant, "phase": phase,
			"round": round_number, "command": command, "log": filename, "exit_code": process.returncode,
			"affinity_cpus": cpus, "workers": workers, "wall_seconds": wall, "cpu_seconds": cpu,
			"background_core_equivalents": background, "hash_match": hash_match,
			"ms_per_tick": float(rows[0][2]) if len(rows) == 1 else None,
			"frame_hash": rows[0][3] if len(rows) == 1 else None,
			"state_hash": int(rows[0][4]) if len(rows) == 1 else None}
		record["accepted"] = process.returncode == 0 and hash_match and background <= args.max_background_cores
		records.append(record)
		save(out/"runs.json", records)
		print(f'{sequence:03d} {name} {variant} {phase}: {record["ms_per_tick"]} ms/tick '
			f'hash={hash_match} background={background:.3f} cores', flush=True)
		return record

	# Fail closed on the language reference. A nonconforming comparison is recorded, never timed.
	reference = ("JaiScript interpreter gate", "jai_gloom", "interpreter", 0, 1)
	gate = run(reference, "A", 300, 0, "gate")
	if not gate["hash_match"] or gate["exit_code"]:
		raise RuntimeError("Interpreter reference hash gate failed")
	valid = []
	for config in CONFIGURATIONS:
		ok = True
		for variant in ("A", "B"):
			for ticks, warmup in ((300,0), (600,0), (600,600)):
				gate = run(config, variant, ticks, warmup, "gate")
				ok = ok and gate["hash_match"] and gate["exit_code"] == 0
		if ok:
			valid.append(config)
		elif config[1] == "jai_gloom":
			raise RuntimeError("JaiScript reference hash gate failed")
	if not args.conformance_only:
		for round_number in range(1, args.rounds+1):
			for config in valid:
				for variant in ("A","B","A","B"):
					for attempt in range(10):
						row = run(config, variant, 600, 600, "timed", round_number)
						if row["accepted"]:
							break
						if not row["hash_match"] or row["exit_code"]:
							raise RuntimeError("Conformance changed during timing")
						time.sleep(2)
					else:
						raise RuntimeError("Machine remained busy; no number credited.")
		if source_fingerprint(root) != sources:
			raise RuntimeError("Sources changed during measurement; results are not a stable snapshot.")
	summary = []
	for config in CONFIGURATIONS:
		name = config[0]
		a = [r["ms_per_tick"] for r in records if r["configuration"] == name and r["phase"] == "timed" and r["accepted"] and r["variant"] == "A"]
		b = [r["ms_per_tick"] for r in records if r["configuration"] == name and r["phase"] == "timed" and r["accepted"] and r["variant"] == "B"]
		row = {"configuration": name, "conforming": config in valid, "cores": config[4]}
		if a and b:
			deltas = [(right/left-1)*100 for left,right in zip(a,b)]
			row.update({"a_median_ms": statistics.median(a), "b_median_ms": statistics.median(b),
				"a_range_ms": [min(a),max(a)], "b_range_ms": [min(b),max(b)],
				"padding_median_delta_percent": (statistics.median(b)/statistics.median(a)-1)*100,
				"padding_paired_band_percent": [min(deltas), max(deltas)], "runs_per_variant": len(a)})
		summary.append(row)
	save(out/"summary.json", summary)
	print(json.dumps(summary, indent=2))

if __name__ == "__main__":
	main()
