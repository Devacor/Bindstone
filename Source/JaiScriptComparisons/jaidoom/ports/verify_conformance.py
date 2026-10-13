#!/usr/bin/env python3
"""Compare per-tic JaiDOOM state, frame length and frame-byte stream hashes."""
import argparse, hashlib, json, pathlib, re, subprocess, time

def digest(path):
	return hashlib.sha256(path.read_bytes()).hexdigest()

def main():
	root=pathlib.Path(__file__).resolve().parents[3]/"JaiScript"
	parser=argparse.ArgumentParser(description=__doc__)
	parser.add_argument("--build",type=pathlib.Path,required=True)
	parser.add_argument("--game-root",type=pathlib.Path,default=root/"examples/jaidoom")
	parser.add_argument("--lua-scripts",type=pathlib.Path)
	parser.add_argument("--output",type=pathlib.Path,required=True)
	parser.add_argument("--backends",default="vm,interpreter")
	parser.add_argument("--repeats",type=int,default=1,help="Additional full replays in the same runtime")
	parser.add_argument("--limit",type=int,default=0,help="Diagnostic prefix only; never a complete conformance result")
	args=parser.parse_args()
	if args.repeats<0 or args.limit<0:parser.error("negative count")
	backends=args.backends.split(",")
	if not backends or any(x not in ("vm","interpreter") for x in backends):parser.error("unknown backend")
	out=args.output.resolve();out.relative_to((pathlib.Path(__file__).resolve().parents[2]/"out").resolve())
	if out.exists():parser.error("use a fresh evidence directory")
	out.mkdir(parents=True)
	game=args.game_root.resolve();scripts=(args.lua_scripts or pathlib.Path(__file__).resolve().parent/"lua").resolve();build=args.build.resolve()
	extension=".exe" if __import__("os").name=="nt" else ""
	exes={n:build/"bin"/(n+extension) for n in ("jai_doom_reference","jai_doom_lua","jai_doom_lua_padding")}
	files=[*game.joinpath("scripts").glob("*.jai"),*scripts.glob("*.lua"),game/"demo.json",game/"wads/doom1.wad",*exes.values()]
	inputs={str(p):digest(p) for p in files}
	(out/"inputs.json").write_text(json.dumps(inputs,indent=2)+"\n",encoding="utf-8")
	runs=[];names=[]
	for backend in backends:
		label="jai-"+backend
		names.append((label,[str(exes["jai_doom_reference"]),str(game/"scripts/p10_timedemo.jai"),str(out/(label+".trace")),backend,str(args.limit),str(args.repeats)]))
	for layout,target in [("normal","jai_doom_lua"),("padding","jai_doom_lua_padding")]:
		label="lua-"+layout
		names.append((label,[str(exes[target]),"--scripts",str(scripts),"--game-root",str(game),"--trace",str(out/(label+".trace")),"--limit",str(args.limit),"--repeat",str(args.repeats),"--",str(game/"demo.json"),"120x78"]))
	for name,command in names:
		start=time.time()
		result=subprocess.run(command,cwd=out,capture_output=True,timeout=1200)
		log=out/(name+".log");log.write_bytes(result.stdout+result.stderr)
		text=(result.stdout+result.stderr).decode("utf-8",errors="replace")
		hashes=[int(x) for x in re.findall(r"STATE_HASH:\s*(\d+)",text)]
		ok=result.returncode==0 and len(hashes)==args.repeats+1 and (args.limit!=0 or all(h==1746772898 for h in hashes))
		runs.append(dict(configuration=name,command=command,exit=result.returncode,seconds=time.time()-start,log=log.name,state_hashes=hashes,hash_gate=ok))
		(out/"runs.json").write_text(json.dumps(runs,indent=2)+"\n",encoding="utf-8")
		print(name,"PASS" if ok else "FAIL",hashes,flush=True)
		if not ok:raise RuntimeError("state/exit gate failed: "+str(log))
	reference=(out/(names[0][0]+".trace")).read_text(encoding="utf-8").splitlines()
	observed= args.limit if args.limit else 3984
	expected_rows=observed*(args.repeats+1)
	comparisons=[]
	for name,_ in names:
		rows=(out/(name+".trace")).read_text(encoding="utf-8").splitlines()
		difference=next((dict(row=i+1,reference=a,actual=b) for i,(a,b) in enumerate(zip(reference,rows)) if a!=b),None)
		equal=rows==reference and len(rows)==expected_rows
		comparisons.append(dict(configuration=name,equal=equal,rows=len(rows),first_difference=difference,last_rows=[x for x in rows if x.split("\t")[1]==str(observed)]))
	unchanged=all(digest(pathlib.Path(p))==sha for p,sha in inputs.items())
	report=dict(complete=args.limit==0,passed=unchanged and all(x["equal"] for x in comparisons),replays=args.repeats+1,tics_per_replay=observed,inputs_unchanged=unchanged,reference=names[0][0],comparisons=comparisons,hash_algorithm="FNV-1a64 over frame bytes, accumulated per replay; every tic also carries state and byte count",timing_eligible=False)
	(out/"comparison.json").write_text(json.dumps(report,indent=2)+"\n",encoding="utf-8")
	print(json.dumps(report,indent=2),flush=True)
	if not report["passed"]:raise SystemExit(1)
if __name__=="__main__":main()
