"""demoreel util (scenes/util.jai): the host API the scenes see (Rng, ESC, host_log),
palettes, the sextant / half-block presenter, the text layer and Gfx. Transitions
(mix_grids, mix_motion) are left out: the perf path runs none.

The presenter's tables (HB_*, TXT_*) are module globals that hb_ensure and
Reel.bind_palettes REBIND, so code outside this module reads them as util.HB_PIX,
util.HB_LUM, ... at the point of use and never imports them by value.

Strings are Python str: utf8(cp) is chr(cp), and the frame becomes the reference's
UTF-8 bytes when the driver encodes it.
"""

import math
import sys

MASK64 = 0xFFFFFFFFFFFFFFFF
ESC = "\x1b"
RESET = ESC + "[0m"


def host_log(line):
	sys.stdout.write(line + "\n")
	sys.stdout.flush()


def idiv(a, b):
	"""C int division (truncates toward zero); Python's // floors."""
	q = a // b
	if q < 0 and q * b != a:
		q += 1
	return q


def imod(a, b):
	"""C int remainder (takes the dividend's sign); Python's % floors."""
	return a - b * idiv(a, b)


class Rng:
	"""xorshift64*, bit-exact with demoreel::demo_rng (demoreel_host.hpp); Python ints never wrap, so each step masks."""

	__slots__ = ("s",)

	def __init__(self, seed):
		self.s = (seed & MASK64) if seed != 0 else 0x9E3779B97F4A7C15
		for _ in range(4):
			self.next_raw()

	def next_raw(self):
		s = self.s
		s ^= s >> 12
		s = (s ^ (s << 25)) & MASK64
		s ^= s >> 27
		self.s = s
		return (s * 0x2545F4914F6CDD1D) & MASK64

	def next(self, n):
		if n <= 0:
			return 0
		return self.next_raw() % n

	def roll(self, lo, hi):
		if hi <= lo:
			return lo
		return lo + self.next(hi - lo + 1)

	def chance(self, percent):
		return self.next(100) < percent

	def nextf(self):
		return (self.next_raw() >> 11) * (1.0 / 9007199254740992.0)


class Palette:
	__slots__ = ("fg", "bg", "rgb")

	def __init__(self):
		self.fg = []
		self.bg = []
		self.rgb = []

	def add(self, r, g, b):
		ir = int(0.0 if r < 0.0 else (255.0 if r > 255.0 else r))
		ig = int(0.0 if g < 0.0 else (255.0 if g > 255.0 else g))
		ib = int(0.0 if b < 0.0 else (255.0 if b > 255.0 else b))
		c = f"{ir};{ig};{ib}m"
		self.fg.append(ESC + "[38;2;" + c)
		self.bg.append(ESC + "[48;2;" + c)
		self.rgb.append(ir * 65536 + ig * 256 + ib)
		return len(self.rgb) - 1

	# n entries lerped from (r0,g0,b0) to (r1,g1,b1); returns the first index
	def ramp(self, n, r0, g0, b0, r1, g1, b1):
		first = len(self.rgb)
		for i in range(n):
			f = i / (n - 1.0) if n > 1 else 0.0
			self.add(r0 + (r1 - r0) * f, g0 + (g1 - g0) * f, b0 + (b1 - b0) * f)
		return first

	# n entries through a list of [r,g,b] stops (piecewise linear); returns the first index
	def gradient(self, n, stops):
		first = len(self.rgb)
		ns = len(stops) - 1
		for i in range(n):
			f = i * ns / (n - 1.0) if n > 1 else 0.0
			k = int(f)
			k = ns - 1 if k >= ns else k
			u = f - k
			a = stops[k]
			b = stops[k + 1]
			ar = float(a[0])
			ag = float(a[1])
			ab = float(a[2])
			br = float(b[0])
			bg2 = float(b[1])
			bb = float(b[2])
			self.add(ar + (br - ar) * u, ag + (bg2 - ag) * u, ab + (bb - ab) * u)
		return first

	def size(self):
		return len(self.rgb)


# ------------------------------------------------------------ presenter ---
# Two cell shapes: half blocks (1x2 pixels a cell) and sextants (2x3, the brightest pixel
# the foreground, the darkest the background). Over the pixels lies a text layer, one
# codepoint per cell (0 for none) with its colour.

HB_PIX = []
HB_FG = []
HB_BG = []
HB_RGB = []
HB_LUM = []
HB_ROWIDX = []
HB_SRC = []       # sextant mode: per glyph pixel column, the grid column it shows
HB_SEXTG = []     # sextant glyphs by TL | TR<<1 | ML<<2 | MR<<3 | BL<<4 | BR<<5
TXT_CP = []
TXT_RGB = []
TXT_USED = False
HB_SEXT = False
HB_W = 0          # grid pixels across
HB_PH = 0         # and down
HB_COLS = 0       # cells
HB_ROWS = 0
HB_WORKERS = 0
HB_HALF = "▀"
HB_FULL = "█"
HB_RESET = ESC + "[0m"


def utf8(cp):
	return chr(cp)


# the grid's pixel size for cols x rows cells
def hb_pixel_w(cols, sext):
	return cols * 3 // 2 if sext else cols


def hb_pixel_h(rows, sext):
	return rows * 3 if sext else rows * 2


def hb_ensure(cols, rows, sext):
	global HB_SEXT, HB_COLS, HB_ROWS, HB_W, HB_PH, HB_PIX, HB_ROWIDX, HB_SRC, TXT_CP, TXT_RGB, TXT_USED, HB_SEXTG
	HB_SEXT = sext
	pw = hb_pixel_w(cols, sext)
	ph = hb_pixel_h(rows, sext)
	if HB_COLS == cols and HB_ROWS == rows and HB_W == pw and HB_PH == ph and len(HB_PIX) == pw * ph:
		return
	HB_COLS = cols
	HB_ROWS = rows
	HB_W = pw
	HB_PH = ph
	HB_PIX = [0] * (pw * ph)
	HB_ROWIDX = list(range(rows))
	HB_SRC = [g * pw // (cols * 2) for g in range(cols * 2)]
	TXT_CP = [0] * (cols * rows)
	TXT_RGB = [0] * (cols * rows)
	TXT_USED = False
	HB_SEXTG = []
	for b in range(64):
		if b == 0:
			HB_SEXTG.append(" ")
		elif b == 21:
			HB_SEXTG.append(utf8(0x258C))
		elif b == 42:
			HB_SEXTG.append(utf8(0x2590))
		elif b == 63:
			HB_SEXTG.append(HB_FULL)
		else:
			HB_SEXTG.append(utf8(0x1FB00 + b - 1 - (1 if b > 21 else 0) - (1 if b > 42 else 0)))


# the text layer: cells, and the pixels they cover
def txt_clear():
	global TXT_USED
	if not TXT_USED:
		return
	TXT_CP[:] = [0] * len(TXT_CP)
	TXT_USED = False


def txt_glyph(col, row, cp, rgb):
	global TXT_USED
	if col < 0 or col >= HB_COLS or row < 0 or row >= HB_ROWS:
		return
	TXT_CP[row * HB_COLS + col] = cp
	TXT_RGB[row * HB_COLS + col] = rgb
	TXT_USED = True


# ASCII text from (col, row); spaces are written too, so a line can clear what lies under it
def txt_put(col, row, s, rgb):
	for i, ch in enumerate(s):
		txt_glyph(col + i, row, ord(ch), rgb)


def cell_col(px):
	return idiv(px * HB_COLS, HB_W)


def cell_row(py):
	return idiv(py * HB_ROWS, HB_PH)


def col_px(col):
	return idiv(col * HB_W, HB_COLS)


def row_px(row):
	return idiv(row * HB_PH, HB_ROWS)


# a text cell: its glyph over the darker of the pixels under it
def hb_text(cell, dark, state):
	result = ""
	if dark != state[1]:
		result += HB_BG[dark]
		state[1] = dark
	c = TXT_RGB[cell]
	state[0] = -1
	cp = TXT_CP[cell]
	return result + f"{ESC}[38;2;{c >> 16};{(c >> 8) & 255};{c & 255}m" + chr(cp)


def hb_row(cy):
	return hb_row_sext(cy) if HB_SEXT else hb_row_half(cy)


def hb_row_half(cy):
	out = []
	app = out.append
	pix = HB_PIX
	lum = HB_LUM
	fgt = HB_FG
	bgt = HB_BG
	txt_cp = TXT_CP
	half = HB_HALF
	full = HB_FULL
	vw = HB_W
	fg = -1
	bg = -1
	ti = cy * 2 * vw
	bi = ti + vw
	cell = cy * vw
	text = TXT_USED
	for x in range(vw):
		top = pix[ti + x]
		bot = pix[bi + x]
		if text and txt_cp[cell + x] != 0:
			state = [fg, bg]
			app(hb_text(cell + x, top if lum[top] < lum[bot] else bot, state))
			fg = state[0]
			bg = state[1]
			continue
		if top == bot:
			if bot == bg:
				app(" ")
			elif top == fg:
				app(full)
			else:
				app(bgt[bot])
				bg = bot
				app(" ")
		else:
			if top != fg:
				app(fgt[top])
				fg = top
			if bot != bg:
				app(bgt[bot])
				bg = bot
			app(half)
	app(HB_RESET)
	return "".join(out)


def hb_row_sext(cy):
	out = []
	app = out.append
	pix = HB_PIX
	lum = HB_LUM
	fgt = HB_FG
	bgt = HB_BG
	src = HB_SRC
	sextg = HB_SEXTG
	txt_cp = TXT_CP
	vw = HB_W
	fg = -1
	bg = -1
	r0 = cy * 3 * vw
	r1 = r0 + vw
	r2 = r1 + vw
	cols = HB_COLS
	cell = cy * cols
	text = TXT_USED
	for cx in range(cols):
		xa = src[cx * 2]
		xb = src[cx * 2 + 1]
		p0 = pix[r0 + xa]
		p1 = pix[r0 + xb]
		p2 = pix[r1 + xa]
		p3 = pix[r1 + xb]
		p4 = pix[r2 + xa]
		p5 = pix[r2 + xb]
		flat = p0 == p1 and p0 == p2 and p0 == p3 and p0 == p4 and p0 == p5
		if flat and not (text and txt_cp[cell] != 0):
			if p0 != bg:
				app(bgt[p0])
				bg = p0
			app(" ")
			cell += 1
			continue
		l0 = lum[p0]
		l1 = lum[p1]
		l2 = lum[p2]
		l3 = lum[p3]
		l4 = lum[p4]
		l5 = lum[p5]
		lmin = l0
		lmax = l0
		fgp = p0
		bgp = p0
		if l1 < lmin:
			lmin = l1
			bgp = p1
		if l1 > lmax:
			lmax = l1
			fgp = p1
		if l2 < lmin:
			lmin = l2
			bgp = p2
		if l2 > lmax:
			lmax = l2
			fgp = p2
		if l3 < lmin:
			lmin = l3
			bgp = p3
		if l3 > lmax:
			lmax = l3
			fgp = p3
		if l4 < lmin:
			lmin = l4
			bgp = p4
		if l4 > lmax:
			lmax = l4
			fgp = p4
		if l5 < lmin:
			lmin = l5
			bgp = p5
		if l5 > lmax:
			lmax = l5
			fgp = p5
		if text and txt_cp[cell] != 0:
			state = [fg, bg]
			app(hb_text(cell, bgp, state))
			fg = state[0]
			bg = state[1]
			cell += 1
			continue
		thr = (lmin + lmax + 1) // 2
		bits = 0
		if l0 >= thr:
			bits = 1
		if l1 >= thr:
			bits += 2
		if l2 >= thr:
			bits += 4
		if l3 >= thr:
			bits += 8
		if l4 >= thr:
			bits += 16
		if l5 >= thr:
			bits += 32
		if fgp != fg:
			app(fgt[fgp])
			fg = fgp
		if bits != 63 and bgp != bg:
			app(bgt[bgp])
			bg = bgp
		app(sextg[bits])
		cell += 1
	app(HB_RESET)
	return "".join(out)


# HB_WORKERS is 0 for the ports: the reference's serial path
def hb_present():
	return [hb_row(cy) for cy in range(HB_ROWS)]


# ------------------------------------------------------------ pixel font ---
# 5x7, one int per row, bit 4 = leftmost column

FONT5 = {
	"A": [14, 17, 17, 31, 17, 17, 17], "B": [30, 17, 17, 30, 17, 17, 30], "C": [15, 16, 16, 16, 16, 16, 15],
	"D": [30, 17, 17, 17, 17, 17, 30], "E": [31, 16, 16, 30, 16, 16, 31], "F": [31, 16, 16, 30, 16, 16, 16],
	"G": [15, 16, 16, 19, 17, 17, 15], "H": [17, 17, 17, 31, 17, 17, 17], "I": [31, 4, 4, 4, 4, 4, 31],
	"J": [31, 2, 2, 2, 18, 18, 12], "K": [17, 18, 20, 24, 20, 18, 17], "L": [16, 16, 16, 16, 16, 16, 31],
	"M": [17, 27, 21, 21, 17, 17, 17], "N": [17, 25, 21, 19, 17, 17, 17], "O": [14, 17, 17, 17, 17, 17, 14],
	"P": [30, 17, 17, 30, 16, 16, 16], "Q": [14, 17, 17, 17, 21, 18, 13], "R": [30, 17, 17, 30, 20, 18, 17],
	"S": [15, 16, 16, 14, 1, 1, 30], "T": [31, 4, 4, 4, 4, 4, 4], "U": [17, 17, 17, 17, 17, 17, 14],
	"V": [17, 17, 17, 17, 17, 10, 4], "W": [17, 17, 17, 21, 21, 21, 10], "X": [17, 17, 10, 4, 10, 17, 17],
	"Y": [17, 17, 10, 4, 4, 4, 4], "Z": [31, 1, 2, 4, 8, 16, 31],
	"0": [14, 17, 19, 21, 25, 17, 14], "1": [4, 12, 4, 4, 4, 4, 14], "2": [14, 17, 1, 2, 4, 8, 31],
	"3": [30, 1, 1, 14, 1, 1, 30], "4": [2, 6, 10, 18, 31, 2, 2], "5": [31, 16, 30, 1, 1, 17, 14],
	"6": [14, 16, 16, 30, 17, 17, 14], "7": [31, 1, 2, 4, 8, 8, 8], "8": [14, 17, 17, 14, 17, 17, 14],
	"9": [14, 17, 17, 15, 1, 1, 14], ".": [0, 0, 0, 0, 0, 12, 12], "!": [4, 4, 4, 4, 4, 0, 4],
	"-": [0, 0, 0, 14, 0, 0, 0], ":": [0, 12, 12, 0, 12, 12, 0], "/": [1, 1, 2, 4, 8, 16, 16],
	"+": [0, 4, 4, 31, 4, 4, 0], "?": [14, 17, 1, 2, 4, 0, 4], "'": [4, 4, 8, 0, 0, 0, 0]
}


class Gfx:
	__slots__ = ()

	def make_grid(self, n):
		return [0] * n

	def make_fgrid(self, n):
		return [0.0] * n

	def rainbow_rgb(self, phase):
		sin = math.sin
		return [128.0 + 127.0 * sin(phase), 128.0 + 127.0 * sin(phase + 2.094), 128.0 + 127.0 * sin(phase + 4.188)]

	def fmt1(self, v):
		t = int(v * 10.0 + 0.5)
		return f"{idiv(t, 10)}.{imod(t, 10)}"

	def text_width(self, text, sx):
		return len(text) * 6 * sx - sx

	# 5x7 text into a pixel grid; rows [row0, row0 + nramp) of the palette shade the glyphs
	# top to bottom, shadow < 0 draws none; only the first `limit` glyphs
	def draw_text(self, pix, w, h, text, x0, y0, sx, sy, row0, nramp, shadow, limit):
		n = len(text) if len(text) < limit else limit
		th = 7 * sy
		for pass_ in range(1 if shadow < 0 else 0, 2):
			ox = 1 if pass_ == 0 else 0
			for li in range(n):
				ch = text[li]
				if ch not in FONT5:
					continue
				bits = FONT5[ch]
				lx = x0 + li * 6 * sx + ox
				for ry in range(7):
					b = bits[ry]
					if b == 0:
						continue
					for dy in range(sy):
						yy = y0 + ry * sy + dy + ox
						if yy < 0 or yy >= h:
							continue
						c = shadow if pass_ == 0 else row0 + ((ry * sy + dy) * nramp) // th
						for rx in range(5):
							if ((b >> (4 - rx)) & 1) == 0:
								continue
							for dx in range(sx):
								xx = lx + rx * sx + dx
								if xx >= 0 and xx < w:
									pix[yy * w + xx] = c

	# replace a full row with centered styled text (style = escape prefix)
	def overlay_center(self, rows, w, y, text, style):
		if y < 0 or y >= len(rows):
			return
		n = len(text)
		if n > w:
			text = text[:w]
			n = w
		pad = (w - n) // 2
		rows[y] = style + " " * pad + text + " " * (w - pad - n) + RESET


gfx = Gfx()


def sgr_fg(r, g, b):
	return f"{ESC}[38;2;{r};{g};{b}m"


def sgr_bg(r, g, b):
	return f"{ESC}[48;2;{r};{g};{b}m"
