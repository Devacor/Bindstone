"""Data tables live in data.py (generated, byte-exact); this file owns the art
parser. Parsed at boot: name -> Art(w, h, pix (flat codes, 0 transparent),
rgb (rgb[code] = (r,g,b) for code 1..n))."""

from typing import NamedTuple

from data import ART_SRC


class Art(NamedTuple):
	w: int
	h: int
	pix: list
	rgb: list


ARTS = {}


def parse_arts():
	ARTS.clear()
	for name, src in ART_SRC.items():
		rows = src["rows"]
		pal = src["pal"]
		codes = {}          # char -> code (first-seen order, row-major)
		rgb = [None]        # rgb[0] unused (0 = transparent)
		pix = []
		for row in rows:
			for ch in row:
				if ch == ".":
					pix.append(0)
				else:
					c = codes.get(ch)
					if c is None:
						c = len(rgb)
						codes[ch] = c
						rgb.append(pal[ch])
					pix.append(c)
		ARTS[name] = Art(len(rows[0]), len(rows), pix, rgb)
