#!/usr/bin/env python3
"""Run GLOOM and JaiDOOM whole-program gates with warmed, pinned, interleaved padding controls."""
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
				if name in ("cl.exe","link.exe","ninja.exe","msbuild.exe","bindstone.exe") or name.startswith(("jai_gloom","gloom_lua","jaiscript")):
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

def parse_timedemo(text, warmed=True):
	text = text.replace("\r\n", "\n")
	parts = text.split("JAI_BENCHMARK_PHASE measured 1\n")
	if warmed and len(parts) != 2:
		return {"hash_match":False,"metric_ms":None,"error":"missing measured main replay"}
	measured = parts[-1].split("JAI_BENCHMARK_END",1)[0] if warmed else text
	hashes = re.findall(r"STATE_HASH:\s*(\d+)",text)
	rows = re.findall(r"played (\d+) tics in (\d+) ms\s*=\s*([\d.]+) ms/tic",measured)
	ok = len(rows)==1 and int(rows[0][0])==3984 and len(hashes)==(2 if warmed else 1) and all(int(h)==1746772898 for h in hashes)
	if warmed:
		ok = ok and "JAI_BENCHMARK_END 1 exit 0" in text
	if not rows:
		return {"hash_match":False,"metric_ms":None,"error":"missing timedemo result"}
	tics,elapsed,reported=rows[-1]
	return {"hash_match":ok,"metric_ms":int(elapsed)/int(tics) if int(tics)>0 else None,
		"reported_ms_per_tic":float(reported),"played_tics":int(tics),"reported_elapsed_ms":int(elapsed),
		"state_hash":int(hashes[-1]) if hashes else None,"frame_hash":None}

def selected_configurations(workloads, engines):
	configs=[]
	if "gloom" in workloads:
		for label,target,backend,workers,cores in CONFIGURATIONS:
			if engines=="jai" and target!="jai_gloom":
				continue
			configs.append({"workload":"gloom","name":label,"target":target,"backend":backend,
				"workers":workers,"cores":cores,"unit":"ms/tick"})
	if "timedemo" in workloads:
		configs.append({"workload":"timedemo","name":"JaiDOOM VM timedemo","target":"jaiscript",
			"backend":"vm","workers":0,"cores":1,"unit":"ms/tic"})
	return configs

def main():
	if os.name!="nt":
		raise SystemExit("This runner records Windows topology and affinity.")
	root=pathlib.Path(__file__).resolve().parents[3]
	parser=argparse.ArgumentParser(description=__doc__)
	parser.add_argument("--build",type=pathlib.Path,default=root/"out/astra/release")
	parser.add_argument("--jai-workload-root",type=pathlib.Path,default=root,help="JaiScript source root containing examples/gloom and examples/jaidoom; use a frozen copy for campaign measurements.")
	parser.add_argument("--compare-build",type=pathlib.Path,help="Interleave a candidate build against the baseline.")
	parser.add_argument("--output",type=pathlib.Path,default=root/"out/astra/whole-program")
	parser.add_argument("--rounds",type=int,default=3)
	parser.add_argument("--workloads",default="gloom,timedemo")
	parser.add_argument("--engines",choices=("all","jai"),default="all")
	parser.add_argument("--max-background-cores",type=float,default=0.20)
	parser.add_argument("--conformance-only",action="store_true")
	parser.add_argument("--recording",default="none",help="Provenance label; profiled timing is not an uninstrumented performance result.")
	args=parser.parse_args()
	workloads=args.workloads.split(",")
	jai_workload_root=args.jai_workload_root.resolve()
	if args.rounds<3 or not workloads or any(w not in ("gloom","timedemo") for w in workloads):
		parser.error("Use at least three rounds and workloads gloom,timedemo.")
	out=args.output.resolve()
	out.relative_to((root/"out/astra").resolve())
	out.mkdir(parents=True,exist_ok=True)
	if (out/"runs.json").exists():
		parser.error("Use a new output directory; existing raw runs are never overwritten.")
	configs=selected_configurations(workloads,args.engines)
	builds={"baseline":args.build.resolve()}
	if args.compare_build:builds["candidate"]=args.compare_build.resolve()
	machine=WindowsMachine()
	# Pin executable identities. Runtime source may advance in Fable's lane after a build.
	binaries={}
	for build in builds.values():
		for config in configs:
			for suffix in ("","_padding"):
				exe=build/"bin"/(config["target"]+suffix+".exe")
				binaries[str(exe)]=file_hash(exe)
	script_paths=list((jai_workload_root/"examples/gloom/scripts").glob("*.jai"))
	script_paths+=list((jai_workload_root/"examples/jaidoom/scripts").glob("*.jai"))
	script_paths+=list((root/"examples/gloom/ports").rglob("*.lua"))
	script_paths += [jai_workload_root/"examples/jaidoom/demo.json",jai_workload_root/"examples/jaidoom/wads/doom1.wad"] if "timedemo" in workloads else []
	workload_hashes={str(p):file_hash(p) for p in script_paths}
	metadata={"runner_sha256":file_hash(pathlib.Path(__file__).resolve()),"created_utc":time.strftime("%Y-%m-%dT%H:%M:%SZ",time.gmtime()),
		"head":subprocess.check_output(["git","rev-parse","HEAD"],cwd=root,text=True).strip(),
		"jai_workload_root":str(jai_workload_root),"builds":{k:str(v) for k,v in builds.items()},"binary_sha256":binaries,"workload_sha256":workload_hashes,
		"cpu_sets":machine.cpu_sets,"performance_cpus":machine.performance_cpus,
		"rounds":args.rounds,"recording":args.recording,"max_background_cores":args.max_background_cores,
		"order":"For each workload/round: normal baseline/candidate, padding baseline/candidate, repeated twice.",
		"warmup":{"gloom":"600 ticks then reboot in the same runtime","timedemo":"initial file/main execution then another main() in the same engine"},
		"units":{"gloom":"ms/tick","timedemo":"reported elapsed integer ms / reported tic count"},
		"competitor_poll_interval_seconds":0.25,
		"padding":"40 retained volatile statements in each executable's cold help function"}
	for name,build in builds.items():
		manifest=build/"build-inputs.json"
		if manifest.exists():metadata.setdefault("build_input_manifests",{})[name]=json.loads(manifest.read_text())
	save(out/"metadata.json",metadata)
	records=[];sequence=0

	def run(config,variant,layout,phase,round_number=0,cold=False,ticks=600):
		nonlocal sequence
		exe=builds[variant]/"bin"/(config["target"]+("_padding" if layout=="padding" else "")+".exe")
		if config["workload"]=="gloom":
			command=[str(exe),"--smoke","--backend",config["backend"],"--workers",str(config["workers"]),
				"--ticks",str(ticks),"--warmup","0" if cold else "600","--seed","666","--pix","quad"]
			if config["target"]=="jai_gloom":command+=["--scripts",str(jai_workload_root/"examples/gloom/scripts")]
		else:
			command=[str(exe),"--no-debug","--no-pause","--backend="+config["backend"]]
			if not cold:command+=["--benchmark-main=1"]
			command += [str(jai_workload_root/"examples/jaidoom/scripts/p10_timedemo.jai"),"--",
				str(jai_workload_root/"examples/jaidoom/demo.json"),"120x78"]
		machine.wait_for_quiet(args.max_background_cores)
		sequence+=1
		filename=f"{sequence:03d}-{config['target']}-w{config['workers']}-{variant}-{layout}-{phase}.log"
		before,start=machine.busy_seconds(),time.perf_counter()
		process=subprocess.Popen(command,cwd=out,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
		cpus=machine.pin(process,config["cores"])
		competing_during = {}
		try:
			while True:
				for competitor in machine.competing_processes():
					if competitor["pid"] != process.pid:
						competing_during[competitor["pid"]] = competitor
				try:
					output,_ = process.communicate(timeout=0.25)
					break
				except subprocess.TimeoutExpired:
					if time.perf_counter()-start >= 300:
						raise
		except subprocess.TimeoutExpired:
			process.kill();output,_=process.communicate()
			(out/filename).write_bytes(output)
			raise RuntimeError("Workload timed out: "+filename)
		except BaseException:
			process.kill();process.communicate()
			raise
		wall=time.perf_counter()-start
		cpu=machine.child_seconds(process)
		background=max(0.0,machine.busy_seconds()-before-cpu)/wall
		competitors=machine.competing_processes()
		(out/filename).write_bytes(output)
		text=output.decode("utf-8",errors="replace")
		if config["workload"]=="gloom":
			rows=ROW.findall(text)
			expected=next(h for h in HASHES if h[0]==ticks)
			ok=len(rows)==1 and rows[0][3]==expected[1] and int(rows[0][4])==expected[2]
			parsed={"hash_match":ok,"metric_ms":float(rows[0][2]) if len(rows)==1 else None,
				"frame_hash":rows[0][3] if len(rows)==1 else None,"state_hash":int(rows[0][4]) if len(rows)==1 else None}
		else:parsed=parse_timedemo(text,not cold)
		record={"sequence":sequence,"configuration":config["name"],"workload":config["workload"],
			"unit":config["unit"],"variant":variant,"layout":layout,"phase":phase,"round":round_number,
			"command":command,"log":filename,"pid":process.pid,"exit_code":process.returncode,
			"affinity_cpus":cpus,"wall_seconds":wall,"cpu_seconds":cpu,
			"background_core_equivalents":background,"competitors_at_end":competitors,
			"competitors_during_run":list(competing_during.values()),**parsed}
		record["accepted"]=process.returncode==0 and parsed["hash_match"] and background<=args.max_background_cores and not competitors and not competing_during
		records.append(record);save(out/"runs.json",records)
		print(f"{sequence:03d} {config['name']} {variant}/{layout} {phase}: {parsed['metric_ms']} {config['unit']}; hash={parsed['hash_match']} background={background:.3f}",flush=True)
		return record

	# GLOOM 300 gate and each warmed full workload establish conformance before timing.
	valid=[]
	for config in configs:
		ok=True
		for variant in builds:
			for layout in ("normal","padding"):
				if config["workload"]=="gloom":
					row=run(config,variant,layout,"gate",cold=True,ticks=300)
					ok=ok and row["hash_match"] and row["exit_code"]==0
				row=run(config,variant,layout,"gate")
				ok=ok and row["hash_match"] and row["exit_code"]==0
		if ok:valid.append(config)
		elif config["target"] in ("jai_gloom","jaiscript"):raise RuntimeError("A JaiScript whole-program gate failed")
	if not args.conformance_only:
		for round_number in range(1,args.rounds+1):
			for config in valid:
				for _ in range(2):
					for layout in ("normal","padding"):
						for variant in builds:
							for attempt in range(10):
								row=run(config,variant,layout,"timed",round_number)
								if row["accepted"]:break
								if not row["hash_match"] or row["exit_code"]:raise RuntimeError("Conformance changed during timing")
								time.sleep(2)
							else:raise RuntimeError("Machine remained busy; the attempts are not credited")
	for path,digest in binaries.items():
		if file_hash(pathlib.Path(path))!=digest:raise RuntimeError("A measured executable changed")
	for path,digest in workload_hashes.items():
		if file_hash(pathlib.Path(path))!=digest:raise RuntimeError("A workload input changed")
	summary=[]
	for config in configs:
		for variant in builds:
			values={layout:[r["metric_ms"] for r in records if r["configuration"]==config["name"] and
				r["variant"]==variant and r["layout"]==layout and r["phase"]=="timed" and r["accepted"]]
				for layout in ("normal","padding")}
			a,b=values["normal"],values["padding"]
			row={"configuration":config["name"],"workload":config["workload"],"unit":config["unit"],
				"variant":variant,"cores":config["cores"],"conforming":config in valid}
			if a and b:
				deltas=[100*(y/x-1) for x,y in zip(a,b)]
				row.update({"normal_median_ms":statistics.median(a),"padding_median_ms":statistics.median(b),
					"normal_range_ms":[min(a),max(a)],"padding_range_ms":[min(b),max(b)],
					"padding_paired_band_percent":[min(deltas),max(deltas)],"runs_per_layout":len(a)})
			summary.append(row)
	save(out/"summary.json",summary)
	if "candidate" in builds:
		differences=[]
		for config in valid:
			for layout in ("normal","padding"):
				series={variant:[r["metric_ms"] for r in records if r["configuration"]==config["name"] and
					r["variant"]==variant and r["layout"]==layout and r["phase"]=="timed" and r["accepted"]] for variant in builds}
				a,b=series["baseline"],series["candidate"]
				if a and b:
					deltas=[100*(y/x-1) for x,y in zip(a,b)]
					differences.append({"configuration":config["name"],"layout":layout,"unit":config["unit"],
						"median_delta_percent":100*(statistics.median(b)/statistics.median(a)-1),
						"paired_delta_band_percent":[min(deltas),max(deltas)]})
		save(out/"differences.json",differences)
	print(json.dumps(summary,indent=2))

if __name__=="__main__":
	main()
