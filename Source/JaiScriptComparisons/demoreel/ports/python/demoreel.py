"""demoreel, standalone Python 3 port: `python demoreel.py --perf N --scene I --w 100 --h 40`.

Python has no embedding host here, so this driver reimplements the part of the shared C++
host (demoreel_host.cpp) the comparison uses: the flags, run_perf's boot / jump / warm-up /
reboot sequence and frame arguments, the FNV-1a 64 scene hash over each frame's UTF-8
bytes, and the printed rows. The host's xorshift64* Rng lives in util.py; the entry points
below are main.jai's. Only --perf is supported: no console, no smoke table.

Timing protocol, as gloom.py's: the timed window covers demo_frame (update, render,
presentation, the frame string). The UTF-8 encode and the FNV-1a hash run outside it,
because in pure Python the hash alone costs whole milliseconds per frame (in the C++
host it is noise); the driver prints that cost on a note line.
"""

import sys

sys.dont_write_bytecode = True

import time

import reel
import julia       # each scene module registers its class in reel.SCENES
import globhunter
import skull
import fluid

PROGRAM = "demoreel.py"
BACKEND = "python"
FNV_OFFSET = 14695981039346656037
FNV_PRIME = 1099511628211
MASK64 = 0xFFFFFFFFFFFFFFFF

REEL = None


# ------------------------------------------------------------ entry points ---
def demo_boot(w, h, seed, backend, smoke, hud_on):
	global REEL
	REEL = reel.Reel(w, h, seed, backend, smoke, hud_on)


def demo_frame(t, dt, key, fps, ms_sim, ms_draw, ms_other):
	return REEL.frame(t, dt, key, fps, ms_sim, ms_draw, ms_other)


def demo_scene_index():
	return REEL.cur_idx


def demo_scene_time():
	return REEL.scene_t


def demo_jump(idx, at_t):
	REEL.jump(idx, float(at_t))


def demo_reload(changed, failed):
	pass


def demo_scene_count():
	return REEL.nscenes


def demo_scene_name():
	return REEL.cur.name


# ----------------------------------------------------------------- harness ---
def fnv1a(hash_, data):
	for b in data:
		hash_ = ((hash_ ^ b) * FNV_PRIME) & MASK64
	return hash_


def run_perf(opt):
	w = opt["width"] if opt["width"] > 0 else 100
	h = opt["height"] if opt["height"] > 0 else 40
	dt = 1.0 / 30.0
	perf_at = opt["perf_at"]
	frames = opt["perf"]
	warmup = opt["perf_warmup"]
	seed = opt["seed"]
	print("%s --perf: %d frames per scene @ %dx%d from scene t=%.2f, seed %d, workers %d"
		% (PROGRAM, frames, w, h, perf_at, seed, opt["workers"]))
	demo_boot(w, h, seed, BACKEND, False, False)
	n = demo_scene_count()
	print("\n%-12s | %2s | %-12s | %9s | %8s | %s" % ("backend", "#", "scene", "ms/frame", "KB/frame", "scene hash"))
	print("-------------+----+--------------+-----------+----------+-----------------")
	total = 0.0
	if opt["scene"] >= n:
		raise RuntimeError("perf scene index is out of range")
	first = 0 if opt["scene"] < 0 else opt["scene"]
	end = n if opt["scene"] < 0 else opt["scene"] + 1
	clock = time.perf_counter
	hash_s = 0.0
	for s in range(first, end):
		if warmup > 0:
			demo_boot(w, h, seed, BACKEND, False, False)
			demo_jump(s, perf_at)
			for f in range(warmup):
				demo_frame(perf_at + f * dt, dt, "", 0.0, 0.0, 0.0, 0.0)
		demo_boot(w, h, seed, BACKEND, False, False)
		demo_jump(s, perf_at)
		scene = demo_scene_name()
		hash_ = FNV_OFFSET
		nbytes = 0
		timed = 0.0
		for f in range(frames):
			t0 = clock()
			try:
				frame = demo_frame(perf_at + f * dt, dt, "", 0.0, 0.0, 0.0, 0.0)
			except Exception as ex:
				raise RuntimeError(f"scene {s} frame {f}[{BACKEND}]: {ex}") from ex
			timed += clock() - t0
			h0 = clock()
			data = frame.encode("utf-8")
			nbytes += len(data)
			hash_ = fnv1a(hash_, data)
			hash_s += clock() - h0
		ms = timed * 1000.0 / frames
		total += ms
		print("%-12s | %2d | %-12s | %9.3f | %8.1f | %016x" % (BACKEND, s + 1, scene, ms, nbytes / frames / 1024.0, hash_))
	print("%-12s | mean %.3f ms/frame over %d scenes" % (BACKEND, total / (end - first), end - first))
	print("(utf-8 encode + fnv1a computed outside the timed window: %.3f ms/frame of pure-python hashing)"
		% (hash_s * 1000.0 / ((end - first) * frames)))
	return 0


USAGE = """demoreel.py - the demoscene tech reel (standalone Python 3 port, --perf only)
  --perf N         headless: N frames of every scene mid-flight, ms/frame + scene hash
  --perf-at T      perf: scene time the measured frames start from (default 3.0)
  --perf-warmup N  untimed frames on the same runtime before each measured scene
  --scene I        select one zero-based scene
  --seed N         reel seed (default 20260705)
  --w N / --h N    grid size in cells (default 100 x 40)
  --workers N      accepted for parity; the port is always serial"""


def main(argv):
	opt = {"seed": 20260705, "perf": 0, "perf_warmup": 0, "perf_at": 3.0, "scene": -1, "width": 0, "height": 0, "workers": 1}
	flags = {"--seed": ("seed", int), "--perf": ("perf", int), "--perf-warmup": ("perf_warmup", int),
		"--perf-at": ("perf_at", float), "--scene": ("scene", int), "--w": ("width", int), "--h": ("height", int),
		"--workers": ("workers", int)}
	i = 1
	while i < len(argv):
		a = argv[i]
		if a in ("--help", "-h"):
			print(USAGE)
			return 0
		if a not in flags:
			print(f"unknown flag: {a}", file=sys.stderr)
			print(USAGE)
			return 2
		if i + 1 >= len(argv):
			print(f"missing value for {a}", file=sys.stderr)
			sys.exit(2)
		key, conv = flags[a]
		opt[key] = conv(argv[i + 1])
		i += 2
	if opt["perf_warmup"] < 0 or opt["scene"] < -1:
		print("invalid perf warmup or scene index", file=sys.stderr)
		return 2
	if opt["perf"] <= 0:
		print("demoreel.py runs --perf only", file=sys.stderr)
		print(USAGE)
		return 2
	try:
		return run_perf(opt)
	except Exception as ex:
		sys.stdout.flush()
		print(f"{PROGRAM}: script error: {ex}", file=sys.stderr)
		return 1


if __name__ == "__main__":
	sys.exit(main(sys.argv))
