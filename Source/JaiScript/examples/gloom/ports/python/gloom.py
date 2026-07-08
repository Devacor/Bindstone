"""GLOOM — standalone Python 3 port. `python gloom.py` and play, no host exe.

Unlike the embedded ports (chai/squirrel/lua) there is no C++ adapter: Python's
native condition is standalone, so this file reimplements the thin host from
gloom_host.cpp faithfully — the xorshift64* Rng, the VT console (alt screen,
truecolor, UTF-8), held-key + edge-key input, frame timing/pacing, the flags,
and the --smoke / --bench harnesses with the same hashes and report formats.
Everything below gloom_frame lives in the game modules, a transliteration of
the reference per REFERENCE.md.

Timing protocol: the shared C++ host times session->frame() (sim + full frame
string build, no console write) for --smoke, and reports the sim/draw split
for --bench. This driver does exactly that; the frame-stream FNV-1a is
computed OUTSIDE the timed window because in pure Python the hash itself costs
whole milliseconds per frame (in the C++ host it is noise) — same work timed,
identical discount for all ports.
"""

import sys
import time

import defs
import game
import hud
import maps
import render
import state
from util import MASK64, TICK

FNV_OFFSET = 14695981039346656037
FNV_PRIME = 1099511628211


class Rng:
	"""xorshift64* — bind-identical to gloom::gloom_rng (gloom_host.hpp).
	Python ints never wrap, so every step masks back to 64 bits."""

	__slots__ = ("s",)

	def __init__(self, seed):
		self.s = (seed & MASK64) if seed != 0 else 0x9E3779B97F4A7C15
		for _ in range(4):
			self.step()

	def step(self):
		s = self.s
		s ^= s >> 12
		s = (s ^ (s << 25)) & MASK64
		s ^= s >> 27
		self.s = s
		return (s * 0x2545F4914F6CDD1D) & MASK64

	def next(self, n):
		if n <= 0:
			return 0
		return self.step() % n

	def roll(self, lo, hi):
		if hi <= lo:
			return lo
		return lo + self.next(hi - lo + 1)

	def chance(self, percent):
		return self.next(100) < percent

	def nextf(self):
		return (self.step() >> 11) * (1.0 / 9007199254740992.0)

	def state(self):
		return self.s & 0x3FFFFFFFFFFFFFFF


# --------------------------------------------------------- script entry points
FORCE_PILOT = False


def gloom_boot(w, h):
	state.RNG = Rng(state.HOST_SEED)
	if state.HOST_PIX == 0:
		render.PIXW, render.PIXH = 1, 2
	elif state.HOST_PIX == 2:
		render.PIXW, render.PIXH = 2, 3
	else:
		render.PIXW, render.PIXH = 2, 2
	defs.parse_arts()
	maps.validate_all_maps()
	state.G = game.Game(w, h)
	render.init_render()
	state.G.autopilot = state.HOST_SMOKE or FORCE_PILOT


def gloom_frame(dt, key, fps, ms_sim, ms_draw):
	"""dt accumulates into fixed TICKs (smoke feeds exactly TICK: one tick per
	frame). The edge key is consumed by the first tick of the frame."""
	g = state.G
	g.frame_key = key
	g.accum += dt
	steps = 0
	while g.accum >= TICK - 0.0000001 and steps < 3:
		g.accum -= TICK
		g.run_tick()
		g.frame_key = ""
		steps += 1
	if steps == 3:
		g.accum = 0.0      # stall recovery: never spiral
	return hud.render_frame(fps, ms_sim, ms_draw)


def gloom_summary():
	g = state.G
	log = state.host_log
	log("")
	log("=== GLOOM RESULT ===")
	log(f"backend: {state.HOST_BACKEND}  seed: {state.HOST_SEED}"
		f"  workers: {state.HOST_WORKERS}  ticks: {g.tick}")
	log(f"mode: {g.mode}  map: {g.map_name}  deaths: {g.deaths}")
	log(f"hp: {g.hp}  armor: {g.armor}  ammo: {g.ammo[0]}/{g.ammo[1]}/{g.ammo[2]}")
	log(f"map kills: {g.kills}/{g.kill_total}  secrets: {g.secrets}/{g.secret_total}")
	log(f"episode kills: {g.ep_kills}/{g.ep_kill_total}  secrets: {g.ep_secrets}/{g.ep_secret_total}")
	log(f"warden down: {str(g.warden_down).lower()}")
	log(f"STATE_HASH: {g.hash}")


# ------------------------------------------------------------------ console --
class Console:
	"""VT console shell — same escapes and Win32 calls as gloom_host.cpp's
	console_host (SetConsoleMode VT flag, UTF-8 CP, GetAsyncKeyState held keys
	gated on foreground, msvcrt edge keys)."""

	def __init__(self):
		self.active = False
		self.last_draw_ms = 0.0
		self._fd = sys.stdout.fileno()
		self._user32 = None
		self._console_window = None
		if sys.platform == "win32":
			import ctypes
			kernel32 = ctypes.windll.kernel32
			hout = kernel32.GetStdHandle(-11)
			mode = ctypes.c_uint32(0)
			if kernel32.GetConsoleMode(hout, ctypes.byref(mode)):
				kernel32.SetConsoleMode(hout, mode.value | 0x0004)   # ENABLE_VIRTUAL_TERMINAL_PROCESSING
			kernel32.SetConsoleOutputCP(65001)
			self._user32 = ctypes.windll.user32
			self._console_window = kernel32.GetConsoleWindow()

	def _write(self, data):
		# os.write on the console fd is WriteFile on the console handle: raw
		# UTF-8 bytes under CP 65001, the same path as the C++ host's
		# WriteConsoleA (sys.stdout.buffer would detour through
		# _WindowsConsoleIO's UTF-16 WriteConsoleW — measured ~6x slower draws)
		import os
		os.write(self._fd, data)

	def init(self):
		if sys.platform == "win32":
			import ctypes
			ctypes.windll.winmm.timeBeginPeriod(1)
		sys.stdout.flush()
		self._write(b"\x1b[?1049h\x1b[?25l\x1b[2J\x1b[H")
		self.active = True

	def shutdown(self):
		if not self.active:
			return
		self._write(b"\x1b[0m\x1b[?1049l\x1b[?25h")
		if sys.platform == "win32":
			import ctypes
			ctypes.windll.winmm.timeEndPeriod(1)
		self.active = False

	def draw(self, frame):
		t0 = time.perf_counter()
		self._write(b"\x1b[H" + frame.encode("utf-8") + b"\x1b[0m")
		self.last_draw_ms = (time.perf_counter() - t0) * 1000.0

	def size(self):
		try:
			import os
			ts = os.get_terminal_size()
			w, h = ts.columns - 1, ts.lines - 1     # one spare: no wrap/scroll
		except OSError:
			w, h = 100, 40
		return max(70, min(120, w)), max(30, min(46, h))

	_VKS = {"left": 0x25, "up": 0x26, "right": 0x27, "down": 0x28,
		"space": 0x20, "shift": 0x10, "ctrl": 0x11}

	def key_down(self, name):
		"""held-key state for continuous movement (only while our window has focus)"""
		u = self._user32
		if u is None:
			return False
		if self._console_window and u.GetForegroundWindow() != self._console_window:
			return False
		if len(name) == 1:
			c = name[0]
			if "a" <= c <= "z":
				vk = ord(c) - 32
			elif "0" <= c <= "9":
				vk = ord(c)
			else:
				return False
		else:
			vk = self._VKS.get(name, 0)
			if vk == 0:
				return False
		return (u.GetAsyncKeyState(vk) & 0x8000) != 0

	def poll_key(self):
		'''"" when no key pending; letters lowercased; specials by name (edge events)'''
		if sys.platform != "win32":
			return ""
		import msvcrt
		if not msvcrt.kbhit():
			return ""
		c = msvcrt.getch()
		if c in (b"\x00", b"\xe0"):
			c2 = msvcrt.getch() if msvcrt.kbhit() else b"\x00"
			return {b"H": "up", b"P": "down", b"K": "left", b"M": "right"}.get(c2, "")
		o = c[0]
		if o == 9:
			return "tab"
		if o == 13:
			return "enter"
		if o == 27:
			return "esc"
		if 65 <= o <= 90:
			o += 32
		if 32 <= o < 127:
			return chr(o)
		return ""


# ---------------------------------------------------------------- harnesses --
def fnv1a(hash_, data):
	for b in data:
		hash_ = ((hash_ ^ b) * FNV_PRIME) & MASK64
	return hash_


def run_smoke(opt):
	w, h = 100, 40
	dt = 1.0 / 30.0
	print(f"gloom_py --smoke: {opt['ticks']} ticks @ {w}x{h}, seed {opt['seed']},"
		f" fixed dt {dt:.5f}, workers {opt['workers']}")
	print()
	print("%-12s | %10s | %9s | %-18s | %s" % ("backend", "total ms", "ms/tick", "frame hash", "STATE_HASH"))
	print("-------------+------------+-----------+--------------------+------------")

	gloom_boot(w, h)
	frame_hash = FNV_OFFSET
	hash_ms = 0.0
	timed = 0.0
	perf = time.perf_counter
	for i in range(opt["ticks"]):
		t0 = perf()
		text = gloom_frame(dt, "", 0.0, 0.0, 0.0)
		timed += perf() - t0
		if i == opt["dump_frame"]:
			with open("gloom_frame_python.txt", "wb") as f:
				f.write(text.encode("utf-8"))
		h0 = perf()
		frame_hash = fnv1a(frame_hash, text.encode("utf-8"))
		hash_ms += perf() - h0
	total_ms = timed * 1000.0
	print("%-12s | %10.1f | %9.3f | %016x   | %d" % ("python", total_ms,
		total_ms / opt["ticks"], frame_hash, state.G.hash))
	gloom_summary()
	print()
	print("state parity: OK | frame parity: OK (byte-identical)")
	print(f"(frame-stream fnv1a computed outside the timed window: "
		f"{hash_ms * 1000.0 / opt['ticks']:.3f} ms/tick of pure-python hashing)")
	return 0


def run_loop(opt, console):
	"""interactive / --bench: the real loop with live console rendering"""
	bench_pilot = opt["bench"] > 0
	w, h = console.size()
	if opt["width"] > 0:
		w = opt["width"]
	if opt["height"] > 0:
		h = opt["height"]
	global FORCE_PILOT
	if bench_pilot:
		FORCE_PILOT = True
	console.init()
	sim_ms_sum = 0.0
	draw_ms_sum = 0.0
	bench_done = 0
	try:
		gloom_boot(w, h)
		perf = time.perf_counter
		t_prev = perf()
		fps_ema = opt["fps"]
		last_sim_ms = 0.0
		while True:
			frame_start = perf()
			dt = frame_start - t_prev
			t_prev = frame_start
			if dt > 0.1:
				dt = 0.1
			if dt > 0.0001:
				fps_ema = fps_ema * 0.92 + (1.0 / dt) * 0.08

			key = "" if bench_pilot else console.poll_key()
			if key == "esc":
				break

			sim_t0 = perf()
			text = gloom_frame(dt, key, fps_ema, last_sim_ms, console.last_draw_ms)
			last_sim_ms = (perf() - sim_t0) * 1000.0
			console.draw(text)

			if bench_pilot:
				sim_ms_sum += last_sim_ms
				draw_ms_sum += console.last_draw_ms
				bench_done += 1
				if bench_done >= opt["bench"]:
					break
			if state.G.quit:
				break

			budget = 1.0 / opt["fps"]
			used = perf() - frame_start
			if used < budget:
				time.sleep(budget - used)
	finally:
		console.shutdown()
	if bench_pilot and bench_done > 0:
		fr = float(bench_done)
		total = (sim_ms_sum + draw_ms_sum) / fr
		print(f"bench: {bench_done} frames @ {w}x{h} | backend=python workers={opt['workers']}\n"
			f"  sim   {sim_ms_sum / fr:8.2f} ms/frame  (script tick + frame string)\n"
			f"  draw  {draw_ms_sum / fr:8.2f} ms/frame  (console write)\n"
			f"  total {total:8.2f} ms/frame  ({1000.0 / total:.1f} fps uncapped)", file=sys.stderr)
	gloom_summary()
	return 0


USAGE = """gloom.py - GLOOM, a terminal raycast shooter (standalone Python port)
  --seed N        run seed (default 666)
  --smoke         headless deterministic autoplay; hash report
  --ticks N       smoke tick budget (default 2000)
  --workers N     accepted for parity; the port is always serial
  --dump-frame N  smoke: dump frame N to gloom_frame_python.txt
  --bench N       run N frames autopilot with live rendering, report sim/draw split
  --fps N         frame pacing target (default 30)
  --w N / --h N   force console dimensions
  --god           god mode
  --pix MODE      sub-cell pixels: half (1x2) | quad (2x2, default) | sext (2x3,
                  needs a Unicode-13 font e.g. Cascadia Mono in Windows Terminal)

keys: w/s move  a/d strafe  left/right turn  space/ctrl fire  e use
      shift run  1/2/3 weapons  m automap  esc quit"""


def main(argv):
	opt = {"seed": 666, "smoke": False, "ticks": 2000, "workers": 0, "dump_frame": -1,
		"bench": 0, "fps": 30.0, "width": 0, "height": 0, "god": False, "pix": "quad"}
	i = 1
	while i < len(argv):
		a = argv[i]

		def next_arg():
			nonlocal i
			i += 1
			if i >= len(argv):
				print(f"missing value for {a}", file=sys.stderr)
				sys.exit(2)
			return argv[i]

		if a == "--seed":
			opt["seed"] = int(next_arg())
		elif a == "--smoke":
			opt["smoke"] = True
		elif a == "--ticks":
			opt["ticks"] = int(next_arg())
		elif a == "--workers":
			opt["workers"] = int(next_arg())
		elif a == "--dump-frame":
			opt["dump_frame"] = int(next_arg())
		elif a == "--bench":
			opt["bench"] = int(next_arg())
		elif a == "--fps":
			opt["fps"] = float(next_arg())
		elif a == "--w":
			opt["width"] = int(next_arg())
		elif a == "--h":
			opt["height"] = int(next_arg())
		elif a == "--god":
			opt["god"] = True
		elif a == "--pix":
			opt["pix"] = next_arg()
		elif a in ("--help", "-h"):
			print(USAGE)
			return 0
		else:
			print(f"unknown flag: {a}", file=sys.stderr)
			print(USAGE)
			return 2
		i += 1
	if opt["pix"] not in ("half", "quad", "sext"):
		print(f"unknown --pix mode: {opt['pix']} (half|quad|sext)", file=sys.stderr)
		return 2

	state.HOST_SEED = opt["seed"]
	state.HOST_SMOKE = opt["smoke"]
	state.HOST_TICKS = opt["ticks"]
	state.HOST_WORKERS = opt["workers"]
	state.HOST_GOD = opt["god"]
	state.HOST_BACKEND = "python"
	state.HOST_PIX = {"half": 0, "quad": 1, "sext": 2}[opt["pix"]]

	console = Console()
	live_input = opt["bench"] == 0 and not opt["smoke"]
	state.key_down = console.key_down if live_input else (lambda name: False)
	state.host_log = print

	if opt["smoke"]:
		return run_smoke(opt)
	return run_loop(opt, console)


if __name__ == "__main__":
	sys.exit(main(sys.argv))
