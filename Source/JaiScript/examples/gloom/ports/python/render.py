"""The renderer. A flat truecolor palette-index pixel grid blitted to the
terminal through sub-cell block glyphs (half / quad / sext), a straight serial
transliteration of the reference pipeline: one DDA ray per cell column,
prebaked background copy, lazy texture strips, far-to-near billboards,
z-clipped particles, then the glyph row builders. Locals-heavy throughout:
every hot table and function is hoisted (that IS idiomatic fast Python)."""

from math import cos, floor, sin
from operator import itemgetter

import enemies
import particles
import state
from data import ENEMY_DEFS, ITEM_DEFS, MAPS
from defs import ARTS
from util import ESC, idivt, PAL_BG, PAL_FG, PAL_LUM, rgb_idx, trunc

PIXW = 2        # pixels per cell horizontally (set by gloom.boot)
PIXH = 2        # pixels per cell vertically
RESET = ""
QUADG = []      # quadrant glyphs indexed by TL|TR<<1|BL<<2|BR<<3
SEXTG = []      # sextant glyphs indexed by TL|TR<<1|ML<<2|MR<<3|BL<<4|BR<<5
HALFG = ""      # upper half block
AM_COLS = []    # automap tile colors (built at boot), [kind]
AM_FLOOR = 0
AM_BLINK = 0
AM_ITEM = 0
AM_ENEMY = 0
AM_SELF = 0
AM_HEAD = 0
HURT_RED = 0
STRIPS = {}     # lazy 33-tall wall texture strips keyed by (kind,side,shade,tx)


def init_render():
	global RESET, HALFG, QUADG, SEXTG, AM_COLS, AM_FLOOR, AM_BLINK, AM_ITEM
	global AM_ENEMY, AM_SELF, AM_HEAD, HURT_RED
	RESET = ESC + "[0m"
	rgb_idx(0, 0, 0)                 # first interned color is black
	HALFG = chr(0x2580)
	QUADG = [" "] + [chr(cp) for cp in (0x2598, 0x259D, 0x2580, 0x2596, 0x258C,
		0x259E, 0x259B, 0x2597, 0x259A, 0x2590, 0x259C, 0x2584, 0x2599, 0x259F, 0x2588)]
	SEXTG = []
	for b in range(64):
		if b == 0:
			SEXTG.append(" ")
		elif b == 21:
			SEXTG.append(chr(0x258C))        # left half
		elif b == 42:
			SEXTG.append(chr(0x2590))        # right half
		elif b == 63:
			SEXTG.append(chr(0x2588))        # full
		else:
			cp = 0x1FB00 + b - 1
			if b > 21:
				cp -= 1
			if b > 42:
				cp -= 1
			SEXTG.append(chr(cp))
	AM_COLS = [0, rgb_idx(150, 140, 120), rgb_idx(110, 150, 110), rgb_idx(110, 120, 160),
		rgb_idx(160, 110, 96), rgb_idx(200, 170, 110), rgb_idx(90, 230, 110),
		rgb_idx(150, 140, 120), rgb_idx(230, 70, 60), rgb_idx(90, 120, 240)]
	AM_FLOOR = rgb_idx(40, 40, 46)
	AM_BLINK = rgb_idx(240, 255, 160)
	AM_ITEM = rgb_idx(255, 220, 80)
	AM_ENEMY = rgb_idx(255, 60, 50)
	AM_SELF = rgb_idx(255, 255, 255)
	AM_HEAD = rgb_idx(255, 250, 140)
	HURT_RED = rgb_idx(200, 30, 24)
	particles.build_particle_colors()


# wall tone table: [kind 0..9][side 0..1][shade 0..15][tone 0..2] flattened;
# tones: 0 base, 1 accent, 2 mortar/frame.
WALLT = []
CEILP = []     # per pixel-row ceiling color, [y]
FLOORP = []    # per pixel-row floor color, [y]


def _push_wall_tones(base, accent):
	for side in range(2):
		facing = 0.70 if side == 1 else 1.0
		for s in range(16):
			lum = (0.13 + 0.87 * s / 15.0) * facing
			WALLT.append(rgb_idx(trunc(base[0] * lum), trunc(base[1] * lum), trunc(base[2] * lum)))
			alum = lum * 1.35
			if alum > 1.0:
				alum = 1.0
			WALLT.append(rgb_idx(trunc(accent[0] * alum), trunc(accent[1] * alum), trunc(accent[2] * alum)))
			mlum = lum * 0.42
			WALLT.append(rgb_idx(trunc(base[0] * mlum), trunc(base[1] * mlum), trunc(base[2] * mlum)))


def build_render_tables(mi):
	global STRIPS
	g = state.G
	theme = MAPS[mi]["theme"]
	WALLT.clear()
	# kind 0 (never rendered): flat dark
	zero = (10.0, 10.0, 12.0)
	_push_wall_tones(zero, zero)
	# kinds 1..4 from the theme
	for k in range(4):
		base = theme["walls"][k]
		_push_wall_tones(base, base)
	# kind 5 door, 6 exit, 7 red door, 8 blue door (episode constants)
	_push_wall_tones((138.0, 116.0, 94.0), (188.0, 164.0, 132.0))
	_push_wall_tones((64.0, 160.0, 84.0), (220.0, 235.0, 120.0))
	_push_wall_tones((176.0, 50.0, 42.0), (245.0, 130.0, 96.0))
	_push_wall_tones((62.0, 94.0, 198.0), (136.0, 176.0, 255.0))

	# ceiling / floor gradients (dark at the horizon), quantized to CELL rows
	CEILP.clear()
	FLOORP.clear()
	crgb = theme["ceil"]
	frgb = theme["floor"]
	vh = g.vh
	half = vh // 2
	for y in range(vh):
		yq = (y // PIXH) * PIXH
		if y < half:
			t = 1.0 * yq / (half - 1) if half > 1 else 1.0
			lum = 1.0 - 0.72 * t
			CEILP.append(rgb_idx(trunc(crgb[0] * lum), trunc(crgb[1] * lum), trunc(crgb[2] * lum)))
			FLOORP.append(0)
		else:
			t2 = 1.0 * (yq - half) / max(1, vh - 1 - half)
			if t2 > 1.0:
				t2 = 1.0
			elif t2 < 0.0:
				t2 = 0.0
			lum2 = 0.26 + 0.74 * t2
			CEILP.append(0)
			FLOORP.append(rgb_idx(trunc(frgb[0] * lum2), trunc(frgb[1] * lum2), trunc(frgb[2] * lum2)))
	# prebaked background: one slice-assign per frame replaces the per-pixel
	# ceiling/floor loops outright
	bg = []
	vw = g.vw
	for y2 in range(vh):
		bg.extend([CEILP[y2] if y2 < half else FLOORP[y2]] * vw)
	g.bg = bg
	# wall tone strips build lazily per (kind, side, shade, tx) combination
	STRIPS = {}
	g.lutc = {}


def _tex_tone(kind, tx, ty):
	"""texture tone at (tx, ty) in 0..31 for a wall kind: 0 base, 1 accent, 2 mortar"""
	if kind == 1:
		tone = ((tx >> 2) + (ty >> 2)) & 1
		if (ty & 7) == 7:
			tone = 2
		return tone
	if kind == 2:
		tone = ((tx + ((ty >> 3 & 1) << 2)) >> 3) & 1
		if (ty & 7) == 0:
			tone = 2
		return tone
	if kind == 3:
		tone = (tx >> 3) & 1
		if (tx & 7) == 0:
			tone = 2
		return tone
	if kind == 4:
		tone = ((tx >> 4) + (ty >> 4)) & 1
		if (tx & 15) == 0 or (ty & 15) == 0:
			tone = 2
		return tone
	if kind == 6:
		tone = ((tx + ty) >> 2) & 1               # hazard stripes
		if ty > 26:
			tone = 2
		return tone
	# doors (plain / red / blue): frame, seam, lock studs
	if tx < 3 or tx > 28 or ty < 2 or ty > 29:
		return 2
	if tx == 15 or tx == 16:
		return 2
	if 14 <= ty <= 17 and (tx & 3) == 1:
		return 1
	return 0


def build_strip(kind, side, shade, tx):
	wallt = WALLT
	tbase = ((kind * 2 + side) * 16 + shade) * 3
	s = [wallt[tbase + _tex_tone(kind, tx, ty)] for ty in range(32)]
	s.append(s[31])      # fixed-point overshoot guard
	return s


# ---------------------------------------------------------- sprite LUTs -----
# per (art, shade band 0..7, flash) palette-code -> palette-index list, cached
_BAND_LUM = (1.0, 0.88, 0.76, 0.64, 0.52, 0.42, 0.32, 0.24)


def sprite_lut(name, band, flash):
	g = state.G
	key = (name, band, flash)
	lut = g.lutc.get(key)
	if lut is not None:
		return lut
	art = ARTS[name]
	lum = _BAND_LUM[band]
	lut = [0]
	for (r, gg, b) in art.rgb[1:]:
		if flash == 1:
			r = min(255, r + 170)
			gg = min(255, gg + 150)
			b = min(255, b + 140)
		lut.append(rgb_idx(trunc(r * lum), trunc(gg * lum), trunc(b * lum)))
	g.lutc[key] = lut
	return lut


# -------------------------------------------------------------- the view ----
def render_view():
	g = state.G
	pix = g.pix
	zbuf = g.zbuf
	vw = g.vw
	vh = g.vh
	pang = g.pang
	dirx = cos(pang)
	diry = sin(pang)
	planex = 0.0 - diry * 0.78
	planey = dirx * 0.78
	px = g.px
	py = g.py
	mw = g.mw
	mk = g.mapkind

	# --- 1+2. rays + columns: ONE ray per cell column (serial DDA, same float
	# stream as the reference's chunked parallel_transform), prebaked
	# background + textured wall slices
	pix[:] = g.bg                      # ceiling + floor in one copy
	pxw = PIXW
	ncols = vw // pxw
	xstep = 2.0 / (ncols - 1.0)
	boost = 2 if g.muzzle > 0 else 0
	strips = STRIPS
	for x in range(ncols):
		camx = x * xstep - 1.0
		rdx = dirx + planex * camx
		rdy = diry + planey * camx
		mx = floor(px)
		my = floor(py)
		adx = -rdx if rdx < 0.0 else rdx
		ady = -rdy if rdy < 0.0 else rdy
		ddx = 100000000.0 if adx < 0.00000001 else 1.0 / adx
		ddy = 100000000.0 if ady < 0.00000001 else 1.0 / ady
		if rdx < 0.0:
			stepx = -1
			sdx = (px - mx) * ddx
		else:
			stepx = 1
			sdx = (mx + 1.0 - px) * ddx
		if rdy < 0.0:
			stepy = -1
			sdy = (py - my) * ddy
		else:
			stepy = 1
			sdy = (my + 1.0 - py) * ddy
		side = 0
		kind = 1
		guard = 0
		while guard < 128:
			if sdx < sdy:
				sdx += ddx
				mx += stepx
				side = 0
			else:
				sdy += ddy
				my += stepy
				side = 1
			c = mk[my * mw + mx]
			if c != 0:
				kind = c
				break
			guard += 1
		dist = sdx - ddx if side == 0 else sdy - ddy
		if dist < 0.02:
			dist = 0.02
		wallx = py + dist * rdy if side == 0 else px + dist * rdx
		wallx -= floor(wallx)
		tx = floor(wallx * 64.0) >> 1             # texcol/2: 0..31

		px0 = x * pxw
		zbuf[px0] = dist
		if pxw == 2:
			zbuf[px0 + 1] = dist
		lineh = floor(vh / dist)                  # 1.0*vh/dist, truncated
		if lineh < 1:
			lineh = 1
		y0 = idivt(vh - lineh, 2)
		y1 = y0 + lineh
		cy0 = 0 if y0 < 0 else y0
		cy1 = vh if y1 > vh else y1
		shade = 15 - floor(dist * 1.55) + boost
		if shade < 0:
			shade = 0
		elif shade > 15:
			shade = 15
		sidx = ((kind * 2 + side) * 16 + shade) * 32 + tx
		strip = strips.get(sidx)
		if strip is None:
			strip = build_strip(kind, side, shade, tx)
			strips[sidx] = strip
		tstep = 65536 // lineh                    # (32 << 11) / lineh
		tacc = (cy0 - y0) * tstep
		gi = cy0 * vw + px0
		if pxw == 2:
			for _ in range(cy0, cy1):
				c = strip[tacc >> 11]
				pix[gi] = c
				pix[gi + 1] = c
				tacc += tstep
				gi += vw
		else:
			for _ in range(cy0, cy1):
				pix[gi] = strip[tacc >> 11]
				tacc += tstep
				gi += vw

	# --- 3. billboards: enemies, pickups, projectiles (far to near) ---
	det = planex * diry - dirx * planey
	if -0.0001 < det < 0.0001:
		det = 0.0001
	inv = 1.0 / det
	sp = []
	for e in g.enemies:
		collect_billboard(sp, e.x, e.y, enemies.enemy_art(e), ENEMY_DEFS[e.kind]["size"], 0.0,
			1 if e.flash > 0 else 0, inv, dirx, diry, planex, planey)
	for it in g.items:
		if it[3] != 1:
			idef = ITEM_DEFS[it[0]]
			collect_billboard(sp, it[1], it[2], idef["art"], idef["size"], 0.0,
				0, inv, dirx, diry, planex, planey)
	for s in g.shots:
		sk = s[0]
		art = "pr_spit" if sk == 1 else ("pr_fire" if sk == 2 else "pr_hex")
		collect_billboard(sp, s[1], s[2], art, 0.16, 0.35, 0, inv, dirx, diry, planex, planey)
	# far first (stable sort keeps equal depths in collect order)
	sp.sort(key=itemgetter(0), reverse=True)
	for rec in sp:
		draw_billboard(rec, pix, zbuf)

	# --- 4. particles: pixel confetti with z-buffer clipping ---
	pcols = particles.PART_COLS
	half2 = vh // 2
	pxh = PIXH
	for prt in g.fx.pool:
		pk = prt[0]
		if pk != 0:
			rx = prt[1] - px
			ry = prt[2] - py
			depth = inv * (planex * ry - planey * rx)
			if 0.15 <= depth <= 14.0:
				ttx = inv * (diry * rx - dirx * ry)
				scx = trunc((vw / 2.0) * (1.0 + ttx / depth))
				if 0 <= scx < vw:
					if depth < zbuf[scx]:
						span = 1.0 * vh / depth
						pyx = trunc(half2 + span * 0.5 - prt[3] * span)
						life = prt[7]
						phase = 3 - min(3, life // 5)
						color = pcols[pk * 4 + phase]
						psz = trunc(0.07 * span) + 1
						if psz > 3:
							psz = 3
						psx = (psz * 2 * pxw) // pxh     # square-ish
						if psx < 1:
							psx = 1
						for oy in range(psz):
							yy = pyx + oy
							if 0 <= yy < vh:
								rowb = yy * vw
								for ox in range(psx):
									xx = scx + ox
									if xx < vw and depth < zbuf[xx]:
										pix[rowb + xx] = color

	# --- 5. overlays: automap, hurt flash ---
	if g.automap:
		draw_automap(pix, g.tiles)
	if g.hurt_flash > 0:
		red = HURT_RED
		thick = pxw * 2
		for x4 in range(vw):
			pix[x4] = red
			pix[vw + x4] = red
			pix[(vh - 2) * vw + x4] = red
			pix[(vh - 1) * vw + x4] = red
		for y4 in range(vh):
			rb = y4 * vw
			for t4 in range(thick):
				pix[rb + t4] = red
				pix[rb + vw - 1 - t4] = red

	# --- 6. pixel grid -> sub-cell glyph rows ---
	if PIXH == 3:
		return rows_sext(pix)
	if PIXW == 2:
		return rows_quad(pix)
	return rows_half(pix)


def rows_half(pix):
	"""half mode: fg = top pixel, bg = bottom pixel, upper-half-block glyph"""
	g = state.G
	vw = g.vw
	fgs = PAL_FG
	bgs = PAL_BG
	hb = HALFG
	reset = RESET
	rows = []
	join = "".join
	for ry in range(g.vh // 2):
		parts = []
		ap = parts.append
		ptop = -1
		pbot = -1
		itop = ry * 2 * vw
		ibot = itop + vw
		for x in range(vw):
			tc = pix[itop + x]
			bc = pix[ibot + x]
			if tc != ptop:
				ap(fgs[tc])
				ptop = tc
			if bc != pbot:
				ap(bgs[bc])
				pbot = bc
			ap(hb)
		ap(reset)
		rows.append(join(parts))
	return rows


def rows_quad(pix):
	"""quad mode: 2x2 pixels per cell, two colors by luminosity partition"""
	g = state.G
	vw = g.vw
	fgs = PAL_FG
	bgs = PAL_BG
	lums = PAL_LUM
	qg = QUADG
	reset = RESET
	rows = []
	join = "".join
	cells = vw // 2
	for ry in range(g.vh // 2):
		parts = []
		ap = parts.append
		pfg = -1
		pbg = -1
		itop = ry * 2 * vw
		ibot = itop + vw
		for cx in range(cells):
			xx = cx * 2
			p0 = pix[itop + xx]
			p1 = pix[itop + xx + 1]
			p2 = pix[ibot + xx]
			p3 = pix[ibot + xx + 1]
			if p0 == p1 == p2 == p3:
				if p0 != pbg:
					ap(bgs[p0])
					pbg = p0
				ap(" ")
			elif p0 == p2 and p1 == p3:
				# vertical halves two-color fast path
				if p0 != pfg:
					ap(fgs[p0])
					pfg = p0
				if p1 != pbg:
					ap(bgs[p1])
					pbg = p1
				ap(qg[5])                # left half (bits 5)
			elif p0 == p1 and p2 == p3:
				if p0 != pfg:
					ap(fgs[p0])
					pfg = p0
				if p2 != pbg:
					ap(bgs[p2])
					pbg = p2
				ap(qg[3])                # upper half (bits 3)
			else:
				l0 = lums[p0]
				l1 = lums[p1]
				l2 = lums[p2]
				l3 = lums[p3]
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
				if fgp != pfg:
					ap(fgs[fgp])
					pfg = fgp
				if bits != 15 and bgp != pbg:
					ap(bgs[bgp])
					pbg = bgp
				ap(qg[bits])
		ap(reset)
		rows.append(join(parts))
	return rows


def rows_sext(pix):
	"""sext mode: 2x3 pixels per cell (Unicode 13 'Symbols for Legacy Computing')"""
	g = state.G
	vw = g.vw
	fgs = PAL_FG
	bgs = PAL_BG
	lums = PAL_LUM
	sg = SEXTG
	reset = RESET
	rows = []
	join = "".join
	cells = vw // 2
	for ry in range(g.vh // 3):
		parts = []
		ap = parts.append
		pfg = -1
		pbg = -1
		r0 = ry * 3 * vw
		r1 = r0 + vw
		r2 = r1 + vw
		for cx in range(cells):
			xx = cx * 2
			p0 = pix[r0 + xx]
			p1 = pix[r0 + xx + 1]
			p2 = pix[r1 + xx]
			p3 = pix[r1 + xx + 1]
			p4 = pix[r2 + xx]
			p5 = pix[r2 + xx + 1]
			if p0 == p1 == p2 == p3 == p4 == p5:
				if p0 != pbg:
					ap(bgs[p0])
					pbg = p0
				ap(" ")
			else:
				l0 = lums[p0]
				l1 = lums[p1]
				l2 = lums[p2]
				l3 = lums[p3]
				l4 = lums[p4]
				l5 = lums[p5]
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
				if fgp != pfg:
					ap(fgs[fgp])
					pfg = fgp
				if bits != 63 and bgp != pbg:
					ap(bgs[bgp])
					pbg = bgp
				ap(sg[bits])
		ap(reset)
		rows.append(join(parts))
	return rows


def collect_billboard(sp, wx, wy, art, size, zlift, flash, inv, dirx, diry, planex, planey):
	"""world -> camera -> screen; appends (depth, scx, art, size, zlift, flash)"""
	g = state.G
	rx = wx - g.px
	ry = wy - g.py
	depth = inv * (planex * ry - planey * rx)
	if depth < 0.18 or depth > 15.0:
		return
	ttx = inv * (diry * rx - dirx * ry)
	scx = trunc((g.vw / 2.0) * (1.0 + ttx / depth))
	if scx < -g.vw or scx > g.vw * 2:
		return
	sp.append((depth, scx, art, size, zlift, flash))


def draw_billboard(rec, pix, zbuf):
	depth, scx, name, size, zlift, flash = rec
	art = ARTS[name]
	aw = art.w
	ah = art.h
	apix = art.pix
	g = state.G
	vw = g.vw
	vh = g.vh
	span = 1.0 * vh / depth
	ph = trunc(span * size)
	if ph < 2:
		ph = 2
	# pixel aspect: a cell is ~1:2 (w:h) split into PIXW x PIXH
	pw = ph * aw * 2 * PIXW // (ah * PIXH)
	if pw < 2:
		pw = 2
	band = trunc(depth * 0.85) - (1 if g.muzzle > 0 else 0)
	if band < 0:
		band = 0
	elif band > 7:
		band = 7
	lut = sprite_lut(name, band, flash)
	bottom = trunc(vh // 2 + span * 0.5 - zlift * span)
	top = bottom - ph
	x0 = scx - pw // 2
	x1 = x0 + pw
	cx0 = 0 if x0 < 0 else x0
	cx1 = vw if x1 > vw else x1
	cy0 = 0 if top < 0 else top
	cy1 = vh if bottom > vh else bottom
	for x in range(cx0, cx1):
		if depth < zbuf[x]:
			acol = (x - x0) * aw // pw
			for y in range(cy0, cy1):
				ay = (y - top) * ah // ph
				code = apix[ay * aw + acol]
				if code > 0:
					pix[y * vw + x] = lut[code]


def draw_automap(pix, tiles):
	"""arcade automap: square tiles, walls in kind colors, live blips"""
	g = state.G
	vw = g.vw
	mw = g.mw
	sx = PIXW * 2
	sy = PIXH
	ox = sx * 2
	oy = sy * 2
	kc = AM_COLS
	am_floor = AM_FLOOR
	for ty in range(g.mh):
		for tx in range(mw):
			t = tiles[ty * mw + tx]
			c = am_floor if t == 0 else kc[t]
			if t == 6 and (g.tick & 8) == 0:
				c = AM_BLINK
			bx = ox + tx * sx
			by = oy + ty * sy
			for yy in range(sy):
				rb = (by + yy) * vw + bx
				for xx in range(sx):
					pix[rb + xx] = c
	# blips: items gold, live enemies red, you white (+ heading pixel)
	for it in g.items:
		if it[3] != 1:
			blip(pix, ox + trunc(it[1]) * sx, oy + trunc(it[2]) * sy, AM_ITEM)
	for e in g.enemies:
		if e.alive:
			blip(pix, ox + trunc(e.x) * sx, oy + trunc(e.y) * sy, AM_ENEMY)
	blip(pix, ox + trunc(g.px) * sx, oy + trunc(g.py) * sy, AM_SELF)
	blip(pix, ox + trunc(g.px + cos(g.pang) * 1.4) * sx, oy + trunc(g.py + sin(g.pang) * 1.4) * sy, AM_HEAD)


def blip(pix, bx, by, c):
	g = state.G
	vw = g.vw
	if bx < 0 or by < 0 or bx + 1 >= vw or by + 1 >= g.vh:
		return
	pix[by * vw + bx] = c
	pix[by * vw + bx + 1] = c
	pix[(by + 1) * vw + bx] = c
	pix[(by + 1) * vw + bx + 1] = c
