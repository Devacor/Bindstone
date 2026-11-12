"""A cell canvas for the showcase HUD and pages (canvas.jai). Each cell is two
stacked pixels (drawn as an upper half block, square on a 1:2 cell) or one
character over the cell's upper pixel. Colors are palette indices."""

import render
from colors import color_idx, mix_rgb
from util import PAL_BG, PAL_FG, idivt

# 3x5 capitals, digits and a little punctuation; '#' is ink
FONT5 = {
	"A": ("###", "#.#", "###", "#.#", "#.#"), "B": ("##.", "#.#", "##.", "#.#", "##."),
	"C": ("###", "#..", "#..", "#..", "###"), "D": ("##.", "#.#", "#.#", "#.#", "##."),
	"E": ("###", "#..", "##.", "#..", "###"), "F": ("###", "#..", "##.", "#..", "#.."),
	"G": ("###", "#..", "#.#", "#.#", "###"), "H": ("#.#", "#.#", "###", "#.#", "#.#"),
	"I": ("###", ".#.", ".#.", ".#.", "###"), "J": ("..#", "..#", "..#", "#.#", "###"),
	"K": ("#.#", "#.#", "##.", "#.#", "#.#"), "L": ("#..", "#..", "#..", "#..", "###"),
	"M": ("#.#", "###", "###", "#.#", "#.#"), "N": ("##.", "#.#", "#.#", "#.#", "#.#"),
	"O": ("###", "#.#", "#.#", "#.#", "###"), "P": ("###", "#.#", "###", "#..", "#.."),
	"Q": ("###", "#.#", "#.#", "###", "..#"), "R": ("##.", "#.#", "##.", "#.#", "#.#"),
	"S": ("###", "#..", "###", "..#", "###"), "T": ("###", ".#.", ".#.", ".#.", ".#."),
	"U": ("#.#", "#.#", "#.#", "#.#", "###"), "V": ("#.#", "#.#", "#.#", "#.#", ".#."),
	"W": ("#.#", "#.#", "###", "###", "#.#"), "X": ("#.#", "#.#", ".#.", "#.#", "#.#"),
	"Y": ("#.#", "#.#", ".#.", ".#.", ".#."), "Z": ("###", "..#", ".#.", "#..", "###"),
	"0": ("###", "#.#", "#.#", "#.#", "###"), "1": (".#.", "##.", ".#.", ".#.", "###"),
	"2": ("###", "..#", "###", "#..", "###"), "3": ("###", "..#", ".##", "..#", "###"),
	"4": ("#.#", "#.#", "###", "..#", "..#"), "5": ("###", "#..", "###", "..#", "###"),
	"6": ("###", "#..", "###", "#.#", "###"), "7": ("###", "..#", ".#.", ".#.", ".#."),
	"8": ("###", "#.#", "###", "#.#", "###"), "9": ("###", "#.#", "###", "..#", "###"),
	"%": ("#.#", "..#", ".#.", "#..", "#.#"), ":": ("...", ".#.", "...", ".#.", "..."),
	"/": ("..#", "..#", ".#.", "#..", "#.."), "-": ("...", "...", "###", "...", "..."),
	"!": (".#.", ".#.", ".#.", "...", ".#."), "'": (".#.", ".#.", "...", "...", "..."),
	".": ("...", "...", "...", "...", ".#."), " ": ("...", "...", "...", "...", "..."),
}


def lettering_width(s, scale):
	"""width in pixels of s in the 3x5 font at a scale, one pixel of spacing per letter"""
	return len(s) * 4 * scale - scale


class Canvas:
	__slots__ = ("w", "h", "px", "glyph", "ink")

	def __init__(self, w, h):
		self.w = w
		self.h = h                       # cells; pixel rows are 2 * h
		self.px = [0] * (w * h * 2)      # palette index per pixel, row-major, w x 2h
		self.glyph = [""] * (w * h)      # "" or the character drawn in the cell
		self.ink = [0] * (w * h)         # its color

	def clear(self, color):
		px = self.px
		for i in range(len(px)):
			px[i] = color
		glyph = self.glyph
		for i in range(len(glyph)):
			glyph[i] = ""

	def dot(self, x, y, color):
		if 0 <= x < self.w and 0 <= y < self.h * 2:
			self.px[y * self.w + x] = color

	def rect(self, x, y, rw, rh, color):
		w = self.w
		px = self.px
		for yy in range(max(0, y), min(self.h * 2, y + rh)):
			for xx in range(max(0, x), min(w, x + rw)):
				px[yy * w + xx] = color

	def gradient(self, y0, y1, top, bottom):
		"""one color per pixel row from top to bottom, from rows y0 to y1 (exclusive)"""
		w = self.w
		px = self.px
		for y in range(max(0, y0), min(self.h * 2, y1)):
			c = color_idx(mix_rgb(top, bottom, 1.0 * (y - y0) / max(1, y1 - y0 - 1)))
			for x in range(w):
				px[y * w + x] = c

	def text(self, x, row, s, color):
		"""text at cell (x, row); characters past the edge are dropped"""
		if row < 0 or row >= self.h:
			return
		w = self.w
		glyph = self.glyph
		ink = self.ink
		for i in range(len(s)):
			cx = x + i
			if cx < 0 or cx >= w:
				continue
			glyph[row * w + cx] = s[i]
			ink[row * w + cx] = color

	def text_centered(self, row, s, color):
		self.text(idivt(self.w - len(s), 2), row, s, color)

	def text_right(self, x_end, row, s, color):
		self.text(x_end - len(s), row, s, color)

	def art(self, a, x, y, lut):
		"""an Art (defs.py) at pixel (x, y), through a code -> color table"""
		aw = a.w
		apix = a.pix
		for ay in range(a.h):
			for ax in range(aw):
				code = apix[ay * aw + ax]
				if code > 0:
					self.dot(x + ax, y + ay, lut[code])

	def lettering(self, s, x, y, scale, shades, shadow):
		"""s in the 3x5 font with its top-left at pixel (x, y), a color per font row, a shadow below right"""
		rect = self.rect
		for pas in range(2):
			for i, ch in enumerate(s):
				glyph_rows = FONT5.get(ch)
				if glyph_rows is None:
					continue
				gx = x + i * 4 * scale
				for ry in range(5):
					for rx in range(3):
						if glyph_rows[ry][rx] != "#":
							continue
						if pas == 0 and shadow >= 0:
							rect(gx + (rx + 1) * scale, y + (ry + 1) * scale, scale, scale, shadow)
						if pas == 1:
							rect(gx + rx * scale, y + ry * scale, scale, scale, shades[ry])

	def rows(self, first, last):
		"""rows first .. last (cells) as terminal lines"""
		w = self.w
		px = self.px
		glyph = self.glyph
		ink = self.ink
		fgs = PAL_FG
		bgs = PAL_BG
		hb = render.HALFG
		reset = render.RESET
		join = "".join
		out = []
		for row in range(first, last):
			parts = []
			ap = parts.append
			fg = -1
			bg = -1
			up0 = row * 2 * w
			lo0 = up0 + w
			c0 = row * w
			for x in range(w):
				cell = c0 + x
				upper = px[up0 + x]
				lower = px[lo0 + x]
				ch = glyph[cell]
				if ch != "":
					if ink[cell] != fg:
						fg = ink[cell]
						ap(fgs[fg])
					if upper != bg:
						bg = upper
						ap(bgs[bg])
					ap(ch)
				elif upper == lower:
					if upper != bg:
						bg = upper
						ap(bgs[bg])
					ap(" ")
				else:
					if upper != fg:
						fg = upper
						ap(fgs[fg])
					if lower != bg:
						bg = lower
						ap(bgs[bg])
					ap(hb)
			ap(reset)
			out.append(join(parts))
		return out
