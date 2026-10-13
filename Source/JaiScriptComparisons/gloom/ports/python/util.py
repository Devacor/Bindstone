"""Small helpers: math, hashing, the truecolor palette intern, text padding.

Python ints are arbitrary-precision, so every place the reference relies on
64-bit unsigned wraparound gets an explicit `& MASK` — the RNG (gloom.Rng),
mix32 below, and nothing else (the sim itself never overflows 53-bit floats).
"""

TICK = 1.0 / 30.0   # fixed sim timestep; --smoke feeds exactly this dt
ESC = "\x1b"

MASK32 = 0xFFFFFFFF
MASK64 = 0xFFFFFFFFFFFFFFFF


def trunc(v):
	"""C-cast truncation toward zero (int() on a float is exactly that)."""
	return int(v)


def idivt(a, b):
	"""int/int division truncating toward zero (Python // floors); for the few
	sites where the numerator can be negative."""
	q = a // b
	if q < 0 and q * b != a:
		q += 1
	return q


def iclamp(v, lo, hi):
	if v < lo:
		return lo
	if v > hi:
		return hi
	return v


def mix32(h, v):
	"""FNV-1a-ish 32-bit lane fold (every folded lane is non-negative)."""
	x = h ^ (v & MASK32)
	x ^= (v >> 32) & MASK32
	return ((x & MASK32) * 16777619) & MASK32


# ------------------------------------------------- truecolor palette intern --
# Interns exact 24-bit colors: pixel grids store small palette indices, the row
# builder emits prebuilt truecolor escapes, and PAL_LUM feeds the quadrant /
# sextant bright/dark cell partition. All 0-based.
PAL_KEYS = {}   # r<<16|g<<8|b -> index
PAL_FG = []     # ESC[38;2;r;g;bm per index
PAL_BG = []     # ESC[48;2;r;g;bm per index
PAL_LUM = []    # 2r+3g+b per index


def rgb_idx(r, g, b):
	r = 0 if r < 0 else (255 if r > 255 else r)
	g = 0 if g < 0 else (255 if g > 255 else g)
	b = 0 if b < 0 else (255 if b > 255 else b)
	key = r * 65536 + g * 256 + b
	idx = PAL_KEYS.get(key)
	if idx is not None:
		return idx
	idx = len(PAL_FG)
	PAL_KEYS[key] = idx
	PAL_FG.append(f"{ESC}[38;2;{r};{g};{b}m")
	PAL_BG.append(f"{ESC}[48;2;{r};{g};{b}m")
	PAL_LUM.append(r * 2 + g * 3 + b)
	return idx


def pad_to(s, n):
	"""pad/truncate plain text to n columns"""
	if len(s) > n:
		return s[:n]
	return s + " " * (n - len(s))


def ang_diff(a, b):
	"""smallest signed angle from a to b, in (-pi, pi]"""
	d = b - a
	while d > 3.14159265:
		d -= 6.2831853
	while d < -3.14159265:
		d += 6.2831853
	return d


def dist2(ax, ay, bx, by):
	dx = ax - bx
	dy = ay - by
	return dx * dx + dy * dy
