#!/usr/bin/env python3
"""Run the JaiScript smoke and the Lua port over a seed/ticks/bots matrix and compare their output.

  python parity.py --jai PATH/jaiscript.exe --lua PATH/lua.exe [--lua PATH2 ...] [--luajit PATH/luajit.exe]
      [--luau PATH/luau.exe] [--ref DIR] [--ticks 500,2000] ...

Both sides run noroundtrip. Timing figures (gen ms, run ms, us/tick) are masked; every other line,
STATE_HASH included, must match byte for byte. --ref reuses saved JaiScript outputs (t{T}_s{S}_b{B}.txt).
"""
import argparse, itertools, os, re, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
GAME = os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(HERE))), 'JaiScript', 'examples', 'gatorquest')

def mask(text):
	text = text.replace('\r\n', '\n')
	text = re.sub(r'\(gen \d+ ms\)', '(gen _ ms)', text)
	text = re.sub(r'ran (\d+) ticks in \d+ ms \(\d+ us/tick\)', r'ran \1 ticks in _ ms (_ us/tick)', text)
	return text

def jai_output(args, t, s, b):
	if args.ref:
		path = os.path.join(args.ref, f't{t}_s{s}_b{b}.txt')
		if os.path.exists(path):
			return open(path, encoding='utf-8', errors='replace').read()
	cmd = [args.jai, '--no-debug', '--no-pause', '--backend=' + args.backend, 'scripts/gatorquest.jai', '--',
		'smoke', 'ticks', str(t), 'seed', str(s), 'bots', str(b), 'noroundtrip']
	out = subprocess.run(cmd, cwd=GAME, capture_output=True, text=True, errors='replace').stdout
	if args.ref:
		os.makedirs(args.ref, exist_ok=True)
		open(os.path.join(args.ref, f't{t}_s{s}_b{b}.txt'), 'w', encoding='utf-8').write(out)
	return out

def main():
	p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
	p.add_argument('--jai', required=True)
	p.add_argument('--backend', default='vm')
	p.add_argument('--lua', action='append', default=[])
	p.add_argument('--luajit', help='a LuaJIT exe: runs luajit/gatorquest.lua with the JIT on and with -joff')
	p.add_argument('--luau', help='a luau CLI: runs luau/gatorquest.luau interpreted (-O1, -O2) and native (-O2 --codegen)')
	p.add_argument('--ref')
	p.add_argument('--ticks', default='500,2000,6000')
	p.add_argument('--seeds', default='1,7,42')
	p.add_argument('--bots', default='1,2,4')
	a = p.parse_args()
	a.jai = os.path.abspath(a.jai)
	runs = [(os.path.basename(x), [os.path.abspath(x), os.path.join(HERE, 'gatorquest.lua')]) for x in a.lua]
	if a.luajit:
		j = os.path.abspath(a.luajit)
		runs.append(('luajit', [j, os.path.join(HERE, 'luajit', 'gatorquest.lua')]))
		runs.append(('luajit-joff', [j, '-joff', os.path.join(HERE, 'luajit', 'gatorquest.lua')]))
	if a.luau:
		u, bundle = os.path.abspath(a.luau), os.path.join(HERE, 'luau', 'gatorquest.luau')
		runs.append(('luau', [u, bundle, '-a']))
		runs.append(('luau-O2', [u, '-O2', bundle, '-a']))
		runs.append(('luau-codegen', [u, '-O2', '--codegen', bundle, '-a']))
	if a.ref: a.ref = os.path.abspath(a.ref)
	fails = 0
	for t, s, b in itertools.product(*[[int(x) for x in v.split(',')] for v in (a.ticks, a.seeds, a.bots)]):
		ref = mask(jai_output(a, t, s, b))
		hj = re.search(r'STATE_HASH: (\d+)', ref)
		row = [f'ticks {t:5d} seed {s:3d} bots {b}', 'jai ' + (hj.group(1) if hj else 'MISSING')]
		for label, cmd in runs:
			out = subprocess.run(cmd + ['smoke', 'ticks', str(t), 'seed', str(s), 'bots', str(b)],
				cwd=GAME, capture_output=True, text=True, errors='replace')
			got = mask(out.stdout + out.stderr)
			hl = re.search(r'STATE_HASH: (\d+)', got)
			same = got == ref and hj is not None
			fails += not same
			row.append(f'{label} {hl.group(1) if hl else "MISSING"} {"OK" if same else "DIFF"}')
			if not same:
				for i, (x, y) in enumerate(itertools.zip_longest(ref.split('\n'), got.split('\n'))):
					if x != y:
						row.append(f'\n    first diff line {i}: jai={x!r}\n    {"":19s}lua={y!r}')
						break
		print(' | '.join(row), flush=True)
	print('ALL MATCH' if fails == 0 else f'{fails} MISMATCHES')
	return 1 if fails else 0

if __name__ == '__main__':
	sys.exit(main())
