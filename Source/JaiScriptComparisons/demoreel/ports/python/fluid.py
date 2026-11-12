"""MAELSTROM (scenes/fluid.jai): neon ink in a slab of black water, simulated twice over and
seen in three dimensions. Jos Stam's stable fluids run on a flow grid a third of the pixel
resolution (forces, vorticity confinement, warm-started SOR pressure, semi-Lagrangian
self-advection); the ink is one number a cell on a grid twice as fine, carried by the same
flow, and picks a place along a row of eight neon tubes. The surface is a height field on the
ink's grid obeying the wave equation, struck by drops, rain, blobs and spray; a shift-lens
camera casts one ray a column across the lit surface, over a neon grid floor.
"""

import math

from music import MUSIC_BAR, MUSIC_BEAT
from reel import Scene, SCENES
from util import gfx

MS_FU = []         # the flow that carries the ink, flow cells per second
MS_FV = []
MS_INK = []        # each ink cell's ink, in palette entries
MS_HOME_X = []     # the ink a cell settles back to: its column's share
MS_HOME_Y = []     # plus its row's,
MS_SETTLE = []     # and by whole ink cells of radius from the tank's middle, the share of the way it goes in a frame
MS_RADIUS = []     # per ink cell: that radius
MS_Z = []          # per ink cell: the surface's height above rest, in ink cells
MS_LIT = []        # per ink cell: its brightness level's first palette entry
MS_GLOW = []       # by radius: the brightness level of flat water,
MS_DUSK = []       # and the share of its light the drain's dark leaves it
MS_INK_ROWS = []
MS_COLS = []
MS_W = 0
MS_H = 0
MS_DW = 0          # the ink grid
MS_DH = 0
MS_CW = 0          # the flow grid
MS_CH = 0
MS_RUN = 0.0       # a frame's step, in ink cells per (flow cell per second)
MS_CX = 0.0        # the tank's middle, in ink cells
MS_CY = 0.0
MS_CAM_X = 0.0     # the camera, in ink cells: where it stands,
MS_CAM_Y = 0.0
MS_CAM_H = 1.0     # how high,
MS_FACE_X = 1.0    # the unit vector it faces along the ground,
MS_FACE_Y = 0.0
MS_FOCAL = 1.0     # pixels to a cell one cell away,
MS_HORIZON = 0.0   # and the row its eye level falls on
MS_DEPTH = 1.0     # the slab's depth
MS_LIGHT_X = 0.0   # brightness levels a unit of slope toward the key light takes away
MS_LIGHT_Y = 0.0
MS_GLINT_X = 0.0   # the slope that glints,
MS_GLINT_Y = 0.0
MS_GLINT_NARROW = 1.0  # one over the square of how far from it a slope may stray,
MS_GLINT = 0.0     # and the levels it adds
MS_CREST = 0.0     # brightness levels a cell of height adds
MS_TUBES = 0       # the first palette entry of the tubes as drawn this frame
MS_PHASE = 0       # how far along the tubes every cell's ink is shifted
MS_FLOOR = 0       # the floor's first palette entry: the dark, then its grid's 8 shades
MS_GRID = 1.0      # floor grid lines to an ink cell
MS_GRID_GAIN = 0.0  # a line's shade where the camera stands,
MS_GRID_FADE = 0.0  # and what a cell of distance takes off it


# one row of ink: each cell takes the flow at its centre (bilinear over the flow grid,
# stepped along the row), traces back one step, blends the four cells it lands among
# and settles toward home
def maelstrom_ink_row(y):
	dw = MS_DW
	out = [0.0] * dw
	if y < 1 or y >= MS_DH - 1:
		return out
	fu = MS_FU
	fv = MS_FV
	ink_old = MS_INK
	home_x = MS_HOME_X
	settle = MS_SETTLE
	radius = MS_RADIUS
	gw = MS_CW
	gymax = MS_CH - 1.001
	xmax = dw - 2.001
	ymax = MS_DH - 2.001
	run = MS_RUN
	home_y = MS_HOME_Y[y]
	qy = y * 0.5 - 0.25
	qy = gymax if qy > gymax else qy
	gy = int(qy)
	ty = qy - gy
	g = gy * gw
	ua = fu[g] + (fu[g + gw] - fu[g]) * ty
	va = fv[g] + (fv[g + gw] - fv[g]) * ty
	x = 1
	radius_at = y * dw
	for gx in range(1, gw):
		g += 1
		ub = fu[g] + (fu[g + gw] - fu[g]) * ty
		vb = fv[g] + (fv[g + gw] - fv[g]) * ty
		du = (ub - ua) * 0.5
		dv = (vb - va) * 0.5
		u = ua + du * 0.5
		v = va + dv * 0.5
		for x in range(x, x + 2):
			px = x - run * u
			py = y - run * v
			px = 1.0 if px < 1.0 else (xmax if px > xmax else px)
			py = 1.0 if py < 1.0 else (ymax if py > ymax else py)
			ix = int(px)
			iy = int(py)
			sx = px - ix
			k = iy * dw + ix
			k2 = k + dw
			a = ink_old[k]
			b = ink_old[k2]
			top = a + (ink_old[k + 1] - a) * sx
			bottom = b + (ink_old[k2 + 1] - b) * sx
			ink = top + (bottom - top) * (py - iy)
			out[x] = ink + (home_y + home_x[x] - ink) * settle[radius[radius_at + x]]
			u += du
			v += dv
		x += 1
		ua = ub
		va = vb
	return out


# one row of the surface's light: each cell's level from its slope (the key light's
# share, and the glint) and its height, at the glow of its radius
def maelstrom_light_row(y):
	dw = MS_DW
	out = [0] * dw
	row = 1 if y < 1 else (MS_DH - 2 if y > MS_DH - 2 else y)
	z = MS_Z
	radii = MS_RADIUS
	glow = MS_GLOW
	dusk = MS_DUSK
	light_x = MS_LIGHT_X
	light_y = MS_LIGHT_Y
	glint_x = MS_GLINT_X
	glint_y = MS_GLINT_Y
	narrow = MS_GLINT_NARROW
	glint = MS_GLINT
	crest = MS_CREST
	k = row * dw + 1
	for x in range(1, dw - 1):
		sx = z[k + 1] - z[k - 1]
		sy = z[k + dw] - z[k - dw]
		radius = radii[k]
		lift = z[k] * crest
		lift = -2.5 if lift < -2.5 else (3.0 if lift > 3.0 else lift)
		level = glow[radius] + lift - sx * light_x - sy * light_y
		ex = sx - glint_x
		ey = sy - glint_y
		near = 1.0 - (ex * ex + ey * ey) * narrow
		if near > 0.0:
			level += near * near * glint
		if level < 2.0:
			level = 2.0
		whole = int(level * dusk[radius])
		if whole > 15:
			whole = 15
		out[x] = whole * 128
		k += 1
	out[0] = out[1]
	out[dw - 1] = out[dw - 2]
	return out


# one column of the picture: its ray across the ground is clipped to the tank; where it
# enters, the slab's cut face hangs from the waterline; then the ray walks the surface
# front to back, a step a row, and each sample that rises above everything nearer fills
# the rows it adds. The rows nothing covered show the floor and its grid.
def maelstrom_column(x):
	hh = MS_H
	dw = MS_DW
	tubes = MS_TUBES
	phase = MS_PHASE
	floor = MS_FLOOR
	z = MS_Z
	ink_at = MS_INK
	lit = MS_LIT
	out = [floor] * hh
	focal = MS_FOCAL
	camx = MS_CAM_X
	camy = MS_CAM_Y
	camh = MS_CAM_H
	hz = MS_HORIZON
	aside = (x + 0.5 - MS_W * 0.5) / focal
	dx = MS_FACE_X - aside * MS_FACE_Y
	dy = MS_FACE_Y + aside * MS_FACE_X
	xhi = dw - 2.01
	yhi = MS_DH - 2.01
	t0 = 1.0
	t1 = 1000000.0
	face = 7.0
	if dx > 0.00001:
		t0 = max(t0, (1.0 - camx) / dx)
		t1 = min(t1, (xhi - camx) / dx)
	elif dx < -0.00001:
		t0 = max(t0, (xhi - camx) / dx)
		t1 = min(t1, (1.0 - camx) / dx)
	elif camx < 1.0 or camx > xhi:
		t1 = 0.0
	across0 = 1.0
	if dy > 0.00001:
		across0 = (1.0 - camy) / dy
		t1 = min(t1, (yhi - camy) / dy)
	elif dy < -0.00001:
		across0 = (yhi - camy) / dy
		t1 = min(t1, (1.0 - camy) / dy)
	elif camy < 1.0 or camy > yhi:
		t1 = 0.0
	if across0 > t0:
		t0 = across0
		face = 5.0
	ytop = hh
	ybase = hh
	if t1 > t0:
		px0 = camx + t0 * dx
		py0 = camy + t0 * dy
		px0 = 1.0 if px0 < 1.0 else (xhi if px0 > xhi else px0)
		py0 = 1.0 if py0 < 1.0 else (yhi if py0 > yhi else py0)
		ix0 = int(px0)
		iy0 = int(py0)
		k0 = iy0 * dw + ix0
		scale0 = focal / t0
		waterline = hz + (camh - z[k0]) * scale0
		foot = hz + (camh + MS_DEPTH) * scale0
		edge_ink = int(ink_at[k0])
		edge_ink = tubes + ((edge_ink + phase) & 127)
		dusk = MS_DUSK[MS_RADIUS[k0]]
		level = face * dusk
		fall = (level - dusk) / (foot - waterline + 0.001)
		ya = 0
		if waterline > 0.0:
			ya = int(waterline)
		else:
			level += fall * waterline
		if ya > hh:
			ya = hh
		if foot < hh:
			ybase = 0 if foot < 0.0 else int(foot)
		if ybase < ya:
			ybase = ya
		for yf in range(ya, ybase):
			whole = int(level)
			out[yf] = edge_ink + whole * 128
			level -= fall
		if ya < ybase:
			meniscus = int(12.0 * dusk)
			out[ya] = edge_ink + meniscus * 128
		ytop = ya
		t = t0
		row_step = 1.0 / (camh * focal)
		while t < t1 and ytop > 0:
			px = camx + t * dx
			py = camy + t * dy
			ix = int(px)
			iy = int(py)
			sx = px - ix
			sy = py - iy
			k = iy * dw + ix
			k2 = k + dw
			za = z[k]
			zb = z[k2]
			ztop = za + (z[k + 1] - za) * sx
			zbot = zb + (z[k2 + 1] - zb) * sx
			row = hz + (camh - ztop - (zbot - ztop) * sy) * focal / t
			if row < ytop:
				ys = 0 if row < 0.0 else int(row)
				if ys < ytop:
					a = ink_at[k]
					b = ink_at[k2]
					top = a + (ink_at[k + 1] - a) * sx
					bottom = b + (ink_at[k2 + 1] - b) * sx
					ink = int(top + (bottom - top) * sy)
					pix = tubes + lit[k] + ((ink + phase) & 127)
					for yy in range(ys, ytop):
						out[yy] = pix
					ytop = ys
			stride = t * t * row_step
			t += 0.35 if stride < 0.35 else (1.0 if stride > 1.0 else stride)
	floor_k = (camh + MS_DEPTH) * focal
	lines = MS_GRID
	line_wide = lines * 1.2 / focal
	gain = MS_GRID_GAIN
	fade = MS_GRID_FADE
	from_ = 0
	to = ytop
	for pass_ in range(2):
		for y in range(from_, to):
			under = y + 0.5 - hz
			if under < 1.0:
				continue
			far = floor_k / under
			shade = gain - far * fade
			if shade < 1.0:
				continue
			wide = far * line_wide
			gx = (camx + far * dx) * lines + 4096.0
			gy = (camy + far * dy) * lines + 4096.0
			wx = int(gx)
			wy = int(gy)
			if gx - wx < wide or gy - wy < wide:
				sh = int(shade)
				out[y] = floor + (8 if sh > 8 else sh)
		from_ = ybase
		to = hh
	return out


class Maelstrom(Scene):
	def __init__(self):
		Scene.__init__(self)
		self.cw = 0                # the flow grid: cw x ch cells, three pixels to a cell
		self.ch = 0
		self.dw = 0                # the ink grid: two cells to a flow cell
		self.dh = 0
		self.unit = 1.0            # ink cells to one of the reference tank's (80 rows)
		self.u = []                # the flow, flow cells per second,
		self.v = []
		self.u1 = []               # and advected
		self.v1 = []
		self.pr = []               # pressure, kept: each solve starts from the last one
		self.dv = []               # divergence
		self.cu = []               # curl
		self.away_x = []           # per flow cell: the unit vector away from the tank's middle,
		self.away_y = []
		self.away_r = []           # and its radius in whole ink cells
		self.radial = []           # by that radius: the speed outward the rings and the drain give the ink
		self.ring_born = []
		self.ring_power = []
		self.ring_lays = []        # 1 for a shock whose wake lays the ink back in its bands
		self.ring_next = 0
		self.hw = []               # the waves: each ink cell's height above rest,
		self.hv = []               # and how fast it is rising, ink cells a second
		self.dish_pair = []        # per ink cell: the share of a vortex's dimple it sits in, the pair's
		self.dish_pool = []        # and the whirlpool's
		self.drop_x = []           # what is in the air, ink cells and ink cells a second
		self.drop_y = []
		self.drop_z = []
		self.drop_vx = []
		self.drop_vy = []
		self.drop_vz = []
		self.drop_size = []        # a blob's radius; spray and rain have none
		self.drop_kind = []        # -1 none, 0 spray, 1 rain, 2 a blob of ink
		self.drop_entry = []       # its colour, along the tubes
		self.drop_next = 0
		self.neon = [              # the tubes: red, green and blue light each
			1.00, 0.08, 0.55,
			0.00, 0.80, 1.00,
			1.00, 0.84, 0.04,
			0.58, 0.20, 1.00,
			0.55, 1.00, 0.00,
			1.00, 0.38, 0.00,
			0.10, 0.35, 1.00,
			0.00, 1.00, 0.50
		]
		self.tubes = 0             # the palette's 128 entries along the tubes, at 16 brightness levels, then all of it negative
		self.floor_at = 0          # then the floor: the dark and 8 shades of grid line, and their negative
		self.jets = 5
		self.sor_iters = 8
		self.vorticity = 12.0
		self.jet_speed = 14.0      # the opening's jets
		self.settling = 0.18       # the share of the way home the ink settles in a second,
		self.pool_settling = 0.6   # and in the whirlpool, where home turns with the water
		self.home_rate = 1.4       # this many radians a second
		self.home_turned = 0.0
		self.pair_speed = 16.0     # how fast each vortex turns at its core's edge, flow cells a second
		self.pool_speed = 18.0
		self.drain_speed = 14.0
		self.wave_speed = 1.0      # ink cells a second: the tank's height in 2 s
		self.wave_drag = 1.0       # ink cells squared a second: what smooths the chop out of the waves
		self.gravity = 1.0         # ink cells a second each second
		self.fall_lead = 0.55      # a blob is let go this long before the kick it lands on,
		self.fall_speed = 1.0      # already falling this fast
		self.tt = 0.0
		self.turn = 1.0            # the way the whirlpool winds
		self.orbit = 0.0           # where the nozzles' ring has turned to
		self.cam_yaw = -0.6        # where the camera's swing has turned to
		self.traded = 0            # the tubes' colours, moved along by the reversal
		self.kick_before = 0.0
		self.kick_ahead_before = 0.0
		self.snare_before = 0.0
		self.rain_due = 0.0
		self.blob_tube = 0
		self.blob_turn = 0
		self.spout_turn = 0
		self.paddle_turn = 0
		self.t_pair = MUSIC_BAR * 2.0
		self.t_pool = MUSIC_BAR * 4.0
		self.t_flip = MUSIC_BAR * 6.0
		self.t_drain = MUSIC_BAR * 8.0
		# the reel picks the grid before build runs, so the budget is set at construction
		self.pixel_budget = 30000

	def length_bars(self):
		return 10

	def hit_times(self):
		return [self.t_pair, self.t_pool, self.t_flip, self.t_drain]

	def hit_levels(self):
		return [1, 2, 1, 2]

	def build(self):
		global MS_W, MS_H, MS_DW, MS_DH, MS_CW, MS_CH, MS_CX, MS_CY, MS_DEPTH, MS_GRID, MS_INK, MS_Z, MS_LIT
		global MS_HOME_X, MS_HOME_Y, MS_INK_ROWS, MS_COLS, MS_SETTLE, MS_GLOW, MS_DUSK, MS_RADIUS
		self.name = "MAELSTROM"
		self.handle = "stable fluids under a wave tank . neon ink"
		self.replay = 20.0   # the water is its whole history
		w = self.w
		h = self.h
		cw = (w + 2) // 3
		ch = (h + 2) // 3
		dw = cw * 2
		dh = ch * 2
		self.cw = cw
		self.ch = ch
		self.dw = dw
		self.dh = dh
		unit = dh / 80.0
		self.unit = unit
		self.wave_speed = dh / 2.0
		self.wave_drag = 2.0 * unit * unit
		self.gravity = 120.0 * unit
		self.fall_speed = 30.0 * unit
		cn = cw * ch
		dn = dw * dh
		self.u = gfx.make_fgrid(cn)
		self.v = gfx.make_fgrid(cn)
		self.u1 = gfx.make_fgrid(cn)
		self.v1 = gfx.make_fgrid(cn)
		self.pr = gfx.make_fgrid(cn)
		self.dv = gfx.make_fgrid(cn)
		self.cu = gfx.make_fgrid(cn)
		self.hw = gfx.make_fgrid(dn)
		self.hv = gfx.make_fgrid(dn)
		MS_W = w
		MS_H = h
		MS_DW = dw
		MS_DH = dh
		MS_CW = cw
		MS_CH = ch
		MS_CX = dw * 0.5 - 0.5
		MS_CY = dh * 0.5 - 0.5
		MS_DEPTH = 10.0 * unit
		MS_GRID = 4.0 / dh
		MS_INK = gfx.make_fgrid(dn)
		MS_Z = gfx.make_fgrid(dn)
		MS_LIT = gfx.make_grid(dn)
		MS_HOME_X = gfx.make_fgrid(dw)
		MS_HOME_Y = gfx.make_fgrid(dh)
		MS_INK_ROWS = []
		for r in range(dh):
			MS_INK_ROWS.append(r)
		MS_COLS = []
		for c in range(w):
			MS_COLS.append(c)
		self.mix_tubes()
		reach = int(math.sqrt(dw * dw + dh * dh * 1.0) * 0.5) + 3
		MS_SETTLE = gfx.make_fgrid(reach)
		MS_GLOW = gfx.make_fgrid(reach)
		MS_DUSK = gfx.make_fgrid(reach)
		self.radial = gfx.make_fgrid(reach)
		MS_RADIUS = gfx.make_grid(dn)
		dish_pair = gfx.make_fgrid(dn)
		dish_pool = gfx.make_fgrid(dn)
		self.dish_pair = dish_pair
		self.dish_pool = dish_pool
		pair_core = 5.0 * unit
		pool_core = 8.0 * unit
		sqrt = math.sqrt
		for y in range(dh):
			ey = y + 0.5 - dh * 0.5
			for x in range(dw):
				ex = x + 0.5 - dw * 0.5
				left = ex + dw * 0.23
				right = ex - dw * 0.23
				at = y * dw + x
				MS_RADIUS[at] = int(sqrt(ex * ex + ey * ey))
				dish_pool[at] = 1.0 / (1.0 + (ex * ex + ey * ey) / (pool_core * pool_core))
				dish_pair[at] = (1.0 / (1.0 + (left * left + ey * ey) / (pair_core * pair_core))
					+ 1.0 / (1.0 + (right * right + ey * ey) / (pair_core * pair_core)))
		away_x = gfx.make_fgrid(cn)
		away_y = gfx.make_fgrid(cn)
		away_r = gfx.make_grid(cn)
		self.away_x = away_x
		self.away_y = away_y
		self.away_r = away_r
		for gy in range(ch):
			fy = gy - (ch - 1) * 0.5
			for gx in range(cw):
				fx = gx - (cw - 1) * 0.5
				far = sqrt(fx * fx + fy * fy)
				i = gy * cw + gx
				away_x[i] = fx / (far + 0.0001)
				away_y[i] = fy / (far + 0.0001)
				away_r[i] = int(far * 2.0)
		for k in range(8):
			self.ring_born.append(-100.0)
			self.ring_power.append(0.0)
			self.ring_lays.append(0.0)
		for d in range(192):
			self.drop_x.append(0.0)
			self.drop_y.append(0.0)
			self.drop_z.append(0.0)
			self.drop_vx.append(0.0)
			self.drop_vy.append(0.0)
			self.drop_vz.append(0.0)
			self.drop_size.append(0.0)
			self.drop_kind.append(-1)
			self.drop_entry.append(0)
		self.lay_bands()
		for settle in range(12):
			self.project()
		self.carry_ink(0.0, 0.0)
		self.shape_surface(0.0)

	# the palette: eight tubes of sixteen entries each, a tube's light swelling from the
	# dark water at its edges to a white-hot core; sixteen levels, from black through
	# full light to the water burning white; then the negative of every entry; then the
	# floor's dark and its grid lines, and their negative
	def mix_tubes(self):
		pal = self.pal
		neon = self.neon
		self.tubes = pal.size()
		gain = [0.0, 0.08, 0.16, 0.25, 0.35, 0.46, 0.58, 0.72, 0.86, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0]
		glare = [0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.12, 0.26, 0.42, 0.6, 0.8, 1.0]
		for negative in range(2):
			for shade in range(16):
				for e in range(128):
					hue = (e // 16) * 3
					off = ((e % 16) + 0.5) / 16.0 - 0.5
					glow = 1.0 - off * off / 0.1156
					glow = 0.0 if glow < 0.0 else glow
					core = (glow - 0.8) * 2.2 if glow > 0.8 else 0.0
					light = 0.05 + 0.95 * glow
					white = core + (1.0 - core) * glare[shade] * (0.3 + 0.7 * glow)
					r = 3.0 + 252.0 * neon[hue] * light
					g = 8.0 + 247.0 * neon[hue + 1] * light
					b = 24.0 + 231.0 * neon[hue + 2] * light
					r = (r + (255.0 - r) * white) * gain[shade]
					g = (g + (255.0 - g) * white) * gain[shade]
					b = (b + (255.0 - b) * white) * gain[shade]
					if negative == 1:
						pal.add(255.0 - r, 255.0 - g, 255.0 - b)
					else:
						pal.add(r, g, b)
		self.floor_at = pal.size()
		for negative2 in range(2):
			for line in range(9):
				lit = line / 8.0
				r2 = 90.0 * lit
				g2 = 14.0 * lit
				b2 = 5.0 + 125.0 * lit
				if negative2 == 1:
					pal.add(255.0 - r2, 255.0 - g2, 255.0 - b2)
				else:
					pal.add(r2, g2, b2)

	# the ramp the ink settles back to: seven tubes to the tank's height, deepening along the unit vector (across, down)
	def lay_home(self, across, down):
		home_x = MS_HOME_X
		home_y = MS_HOME_Y
		cx = MS_CX
		cy = MS_CY
		tube = 112.0 / self.dh
		for x in range(self.dw):
			home_x[x] = (x - cx) * across * tube
		for y in range(self.dh):
			home_y[y] = 1080.0 + (y - cy) * down * tube

	# the opening: the ink in its bands, across jets that run against their neighbours,
	# their seams already rippling so the shear rolls them up
	def lay_bands(self):
		self.lay_home(0.0, 1.0)
		ink = MS_INK
		dw = self.dw
		dh = self.dh
		cw = self.cw
		ch = self.ch
		jets = self.jets
		u = self.u
		v = self.v
		rng = self.rng
		sin = math.sin
		for y in range(dh):
			i = y * dw
			for x in range(dw):
				ink[i] = MS_HOME_Y[y]
				i += 1
		phase = []
		for s in range(jets + 1):
			phase.append(rng.nextf() * 6.2831853)
			phase.append(rng.nextf() * 6.2831853)
		for gy in range(1, ch - 1):
			across = (gy + 0.5) * jets / ch
			seam_at = int(across + 0.5)
			ripple = math.cos(3.14159265 * across)
			ripple = 0.0 if seam_at == 0 or seam_at == jets else ripple * ripple * ripple * ripple
			for gx in range(1, cw - 1):
				j = gy * cw + gx
				u[j] = self.band_jet(gy)
				along = gx * 6.2831853 / cw
				v[j] = self.jet_speed * self.unit * ripple * (0.3 * sin(along * 5.0 + phase[seam_at * 2]) + 0.2 * sin(along * 3.0 + phase[seam_at * 2 + 1]))

	# a flow row's place in the opening's jets: alternate ways, shearing at the seams
	def band_jet(self, gy):
		wave = math.sin(3.14159265 * (gy + 0.5) * self.jets / self.ch) * 3.0
		steep = math.exp(wave)
		return self.jet_speed * self.unit * (steep - 1.0 / steep) / (steep + 1.0 / steep)

	def smooth01(self, x):
		c = 0.0 if x < 0.0 else (1.0 if x > 1.0 else x)
		return c * c * (3.0 - 2.0 * c)

	def kick_at(self, t):
		return 0.0 if self.cue is None else self.cue.kick_env(t)

	def snare_at(self, t):
		return 0.0 if self.cue is None else self.cue.snare_env(t)

	def hit_at(self, t):
		return 0.0 if self.cue is None else self.cue.hit(t)

	def update(self, t, dt):
		before = self.tt
		self.tt = t
		if dt <= 0.0:
			return
		self.vorticity = 9.0 + 9.0 * (0.6 if self.cue is None else self.cue.energy(t))
		step = 0.05 if dt > 0.05 else dt
		self.strike(before, t)
		self.stir(t, step)
		self.confine(step)
		self.project()
		self.advect_flow(step)
		self.carry_ink(t, step)
		self.throw_water(before, t, step)
		self.ripple(step)
		self.fall(step)
		self.cam_yaw += self.turn * (0.35 + 0.5 * self.smooth01((t - self.t_drain) / (MUSIC_BAR * 2.0))) * step
		self.shape_surface(t)

	# the hits, once each as the clock crosses them: a ring of light for the pair, and a
	# shock for the whirlpool, the reversal (the whole flow turning back on itself, the
	# tank sloshing, every tube trading its colour for the next) and the drain, whose
	# funnel plunges; and each kick, a wave: off a side wall, then out of the whirlpool's eye
	def strike(self, before, t):
		unit = self.unit
		if before < self.t_pair and t >= self.t_pair:
			self.ring(self.t_pair, 0.8, 0.0)
		if before < self.t_pool and t >= self.t_pool:
			self.ring(self.t_pool, 1.0, 1.0)
		if before < self.t_flip and t >= self.t_flip:
			self.ring(self.t_flip, 1.0, 1.0)
			self.turn = 0.0 - self.turn
			self.traded += 16
			cn = self.cw * self.ch
			u = self.u
			v = self.v
			pr = self.pr
			for i in range(cn):
				u[i] = 0.0 - u[i]
				v[i] = 0.0 - v[i]
				pr[i] = 0.0 - pr[i]
			vel = self.hv
			dw = self.dw
			cx = MS_CX
			tilt = 12.0 * unit * self.turn / (dw * 0.5)
			for y in range(1, self.dh - 1):
				j = y * dw + 1
				for x in range(1, dw - 1):
					vel[j] += (x - cx) * tilt
					j += 1
		if before < self.t_drain and t >= self.t_drain:
			self.ring(self.t_drain, 1.0, 1.0)
			self.splash(MS_CX, MS_CY, 12.0 * unit, 9.0 * unit)
			self.crown(MS_CX, MS_CY, 22, 4.0 * unit, 70.0 * unit, 5)
		kick = self.kick_at(t)
		if kick > self.kick_before + 0.3:
			if t > self.t_pool:
				self.ring(t, 0.3 * kick, 0.0)
				self.bump(MS_CX, MS_CY, 45.0 * unit * kick, 7.0 * unit)
			else:
				self.paddle(45.0 * unit * kick)
		self.kick_before = kick

	# a side wall shoves the water beside it up and the water beyond that down, the left
	# wall and the right in turn: a wave as long as the tank sets out across it
	def paddle(self, speed):
		vel = self.hv
		dw = self.dw
		cos = math.cos
		self.paddle_turn = 1 - self.paddle_turn
		paddle_turn = self.paddle_turn
		for y in range(1, self.dh - 1):
			row = y * dw
			for k in range(8):
				x = 1 + k
				if paddle_turn == 1:
					x = dw - 2 - k
				vel[row + x] += speed * cos(0.44879895 * k)

	def ring(self, born, power, lays):
		self.ring_born[self.ring_next] = born
		self.ring_power[self.ring_next] = power
		self.ring_lays[self.ring_next] = lays
		self.ring_next = (self.ring_next + 1) % len(self.ring_born)

	# the choreography, in ink cells: the opening's jets, then a counter-rotating pair
	# with a rod stirring between them, then one whirlpool (home turning with it) that
	# the drain tightens, six nozzles orbiting from the pair on
	def stir(self, t, step):
		sin = math.sin
		cos = math.cos
		cx = MS_CX
		cy = MS_CY
		unit = self.unit
		dw = self.dw
		dh = self.dh
		ch = self.ch
		turn = self.turn
		kick = self.kick_at(t)
		snare = self.snare_at(t)
		if t < self.t_pair:
			self.drive_jets((0.5 + 1.6 * kick) * step)
			return
		drain = self.smooth01((t - self.t_drain) / (MUSIC_BAR * 1.5))
		if t >= self.t_pool:
			pool = self.smooth01((t - self.t_pool) / MUSIC_BEAT)
			self.home_turned += turn * self.home_rate * (1.0 + drain) * pool * step
			self.lay_home(0.0 - sin(self.home_turned), cos(self.home_turned))
			self.whirl(cx, cy, turn * unit * (self.pool_speed + self.drain_speed * drain) * pool, ch * (0.26 - 0.1 * drain), (4.0 + 2.0 * drain) * step)
		else:
			pair = self.smooth01((t - self.t_pair) / MUSIC_BEAT)
			self.whirl(cx - dw * 0.23, cy, 0.0 - self.pair_speed * unit * pair, ch * 0.2, 3.5 * step)
			self.whirl(cx + dw * 0.23, cy, self.pair_speed * unit * pair, ch * 0.2, 3.5 * step)
			a = t * 1.3
			rod_x = cx + dw * 0.3 * sin(a)
			rod_y = cy + dh * 0.27 * sin(a * 2.0)
			self.drag(rod_x, rod_y, dw * 0.195 * cos(a), dh * 0.351 * cos(a * 2.0), 4.0 * unit)
			self.bump(rod_x, rod_y, 0.0 - 90.0 * unit * step, 3.5 * unit)
		rush = 0.0 if self.cue is None else self.cue.riser(t)
		self.orbit += turn * (0.55 + 1.2 * rush * rush + 1.4 * drain) * step
		orbit = self.orbit
		reach = 1.0 - 0.5 * drain
		for k in range(6):
			a2 = orbit + k * 1.0471976
			ca = cos(a2)
			sa = sin(a2)
			pours = k % 2 == 0
			beat = kick if pours else snare
			aim = 0.55 if pours else 1.4
			speed = (5.0 + 22.0 * beat) * unit / math.sqrt(1.0 + aim * aim)
			x = cx + ca * dw * 0.37 * reach
			y = cy + sa * dh * 0.37 * reach
			self.drag(x, y, (0.0 - sa * turn - ca * aim) * speed, (ca * turn - sa * aim) * speed, 3.5 * unit)
			self.pour(x, y, (1.0 if pours else -1.0) * (12.0 + 80.0 * beat) * step, 4.5 * unit)

	# the opening's jets pull the flow back toward their shear
	def drive_jets(self, grip):
		u = self.u
		cw = self.cw
		for gy in range(1, self.ch - 1):
			jet = self.band_jet(gy)
			i = gy * cw + 1
			for gx in range(1, cw - 1):
				u[i] += (jet - u[i]) * grip
				i += 1

	# a disc of `rad` ink cells about ink cell (x, y) pulls the flow toward (vx, vy), hardest at its centre
	def drag(self, x, y, vx, vy, rad):
		u = self.u
		v = self.v
		cw = self.cw
		qx = x * 0.5 - 0.25
		qy = y * 0.5 - 0.25
		qr = rad * 0.5 + 0.75
		qr2 = qr * qr
		x0 = max(1, int(qx - qr))
		x1 = min(cw - 2, int(qx + qr) + 1)
		y0 = max(1, int(qy - qr))
		y1 = min(self.ch - 2, int(qy + qr) + 1)
		for gy in range(y0, y1 + 1):
			ey = gy - qy
			for gx in range(x0, x1 + 1):
				ex = gx - qx
				q = ex * ex + ey * ey
				if q >= qr2:
					continue
				f = (1.0 - q / qr2) * 0.6
				i = gy * cw + gx
				u[i] += (vx - u[i]) * f
				v[i] += (vy - v[i]) * f

	# ink poured into (or, negative, drawn out of) a disc about ink cell (x, y), most at its centre
	def pour(self, x, y, amount, rad):
		ink = MS_INK
		dw = self.dw
		r2 = rad * rad
		x0 = max(1, int(x - rad))
		x1 = min(dw - 2, int(x + rad) + 1)
		y0 = max(1, int(y - rad))
		y1 = min(self.dh - 2, int(y + rad) + 1)
		for py in range(y0, y1 + 1):
			ey = py - y
			for px in range(x0, x1 + 1):
				ex = px - x
				q = ex * ex + ey * ey
				if q >= r2:
					continue
				fall = 1.0 - q / r2
				ink[py * dw + px] += amount * fall * fall

	# a vortex at ink cell (x, y): the flow is pulled toward a swirl about it that turns
	# `speed` flow cells a second at `core` cells out and falls away beyond
	def whirl(self, x, y, speed, core, grip):
		u = self.u
		v = self.v
		cw = self.cw
		qx = x * 0.5 - 0.25
		qy = y * 0.5 - 0.25
		c2 = core * core
		k = 2.0 * speed / core
		for gy in range(1, self.ch - 1):
			ey = gy - qy
			i = gy * cw + 1
			for gx in range(1, cw - 1):
				ex = gx - qx
				fall = 1.0 / (1.0 + (ex * ex + ey * ey) / c2)
				pull = grip * fall
				u[i] += (0.0 - ey * k * fall - u[i]) * pull
				v[i] += (ex * k * fall - v[i]) * pull
				i += 1

	# vorticity confinement: push along N x curl, N the unit gradient of |curl|
	def confine(self, step):
		uu = self.u
		vv = self.v
		c = self.cu
		gw = self.cw
		gh = self.ch
		sqrt = math.sqrt
		for y in range(1, gh - 1):
			i = y * gw + 1
			for x in range(1, gw - 1):
				c[i] = 0.5 * (vv[i + 1] - vv[i - 1] - uu[i + gw] + uu[i - gw])
				i += 1
		eps = self.vorticity * step
		for y2 in range(2, gh - 2):
			j = y2 * gw + 2
			for x2 in range(2, gw - 2):
				cr = c[j + 1]
				cl = c[j - 1]
				cd = c[j + gw]
				cn = c[j - gw]
				gx = (-cr if cr < 0.0 else cr) - (-cl if cl < 0.0 else cl)
				gy = (-cd if cd < 0.0 else cd) - (-cn if cn < 0.0 else cn)
				f = eps * c[j] / (sqrt(gx * gx + gy * gy) + 0.00001)
				uu[j] += gy * f
				vv[j] -= gx * f
				j += 1

	# make the flow divergence-free: solve laplace(p) = div(u) by successive
	# over-relaxation from last frame's pressure (walls are Neumann: a border cell
	# copies its inner neighbour), then subtract grad(p)
	def project(self):
		uu = self.u
		vv = self.v
		p = self.pr
		dvg = self.dv
		gw = self.cw
		gh = self.ch
		for y in range(1, gh - 1):
			i = y * gw + 1
			for x in range(1, gw - 1):
				dvg[i] = 0.5 * (uu[i + 1] - uu[i - 1] + vv[i + gw] - vv[i - gw])
				i += 1
		last = (gh - 1) * gw
		for it in range(self.sor_iters):
			for y2 in range(1, gh - 1):
				j = y2 * gw + 1
				for x2 in range(1, gw - 1):
					g = (p[j - 1] + p[j + 1] + p[j - gw] + p[j + gw] - dvg[j]) * 0.25
					p[j] += 1.7 * (g - p[j])
					j += 1
			for x3 in range(gw):
				p[x3] = p[x3 + gw]
				p[last + x3] = p[last - gw + x3]
			for y3 in range(gh):
				r = y3 * gw
				p[r] = p[r + 1]
				p[r + gw - 1] = p[r + gw - 2]
		for y4 in range(1, gh - 1):
			k = y4 * gw + 1
			for x4 in range(1, gw - 1):
				uu[k] -= 0.5 * (p[k + 1] - p[k - 1])
				vv[k] -= 0.5 * (p[k + gw] - p[k - gw])
				k += 1

	# semi-Lagrangian: each cell traces its velocity back one step and takes the
	# bilinear blend of the four cells it lands among; the walls hold still and
	# the water slowly calms
	def advect_flow(self, step):
		uu = self.u
		vv = self.v
		un = self.u1
		vn = self.v1
		gw = self.cw
		gh = self.ch
		xmax = gw - 1.001
		ymax = gh - 1.001
		keep = 1.0 - 0.15 * step
		for y in range(1, gh - 1):
			i = y * gw + 1
			for x in range(1, gw - 1):
				px = x - step * uu[i]
				py = y - step * vv[i]
				px = 0.0 if px < 0.0 else (xmax if px > xmax else px)
				py = 0.0 if py < 0.0 else (ymax if py > ymax else py)
				ix = int(px)
				iy = int(py)
				sx = px - ix
				sy = py - iy
				k = iy * gw + ix
				top = uu[k] + (uu[k + 1] - uu[k]) * sx
				bot = uu[k + gw] + (uu[k + gw + 1] - uu[k + gw]) * sx
				un[i] = (top + (bot - top) * sy) * keep
				top2 = vv[k] + (vv[k + 1] - vv[k]) * sx
				bot2 = vv[k + gw] + (vv[k + gw + 1] - vv[k + gw]) * sx
				vn[i] = (top2 + (bot2 - top2) * sy) * keep
				i += 1
		# the original's u = u1 copies (C++ value semantics): the two stay separate arrays
		uu[:] = un
		vv[:] = vn

	# what the tank's middle sends out, by radius: every ring's lit crest, dark wake and
	# shove outward (a shock's wake also lays the ink back in its bands), the drain's
	# pull inward, its white eye and the dark closing in on it
	def radiate(self, t, step):
		settle = MS_SETTLE
		push = self.radial
		lit = MS_GLOW
		kept = MS_DUSK
		unit = self.unit
		t_drain = self.t_drain
		n = len(lit)
		drain = self.smooth01((t - t_drain) / MUSIC_BAR)
		closing = self.smooth01((t - t_drain - MUSIC_BAR * 0.5) / (MUSIC_BAR * 1.5))
		dark_at = n * (1.0 - closing) + 11.0 * unit * closing
		dusk = 0.25 / unit
		eye = 3.5 * unit
		mouth = 9.0 * unit
		pull = 26.0 * unit * drain
		calm = (self.settling + (self.pool_settling - self.settling) * self.smooth01((t - self.t_pool) / MUSIC_BEAT)) * step
		full = 8.0 + 2.5 * self.hit_at(t)
		for r in range(n):
			m = r / mouth
			q = r / eye
			push[r] = 0.0 - pull * m / (1.0 + m * m)
			lit[r] = full + 5.0 * drain / (1.0 + q * q * q * q)
			settle[r] = calm
		wide = 3.5 * unit
		ring_born = self.ring_born
		ring_power = self.ring_power
		ring_lays = self.ring_lays
		exp = math.exp
		for k in range(len(ring_born)):
			age = t - ring_born[k]
			if age < 0.0 or age > 2.2:
				continue
			amp = ring_power[k] * exp(0.0 - age * 1.5)
			at = (3.0 + age * 48.0) * unit
			r0 = max(0, int(at - wide * 2.6))
			r1 = min(n - 1, int(at + wide) + 1)
			for r2 in range(r0, r1 + 1):
				e = (r2 - at) / wide
				if e > -1.0:
					crest = 1.0 - e * e
					if crest > 0.0:
						lit[r2] += 3.4 * amp * crest
						push[r2] += 24.0 * unit * amp * crest
				else:
					wake = 1.0 - (e + 1.8) * (e + 1.8) / 0.64
					if wake > 0.0:
						lit[r2] -= 3.0 * amp * wake
						settle[r2] += ring_lays[k] * wake
		for r3 in range(n):
			if settle[r3] > 1.0:
				settle[r3] = 1.0
			left = (dark_at - r3) * dusk
			kept[r3] = 0.0 if left < 0.0 else (1.0 if left > 1.0 else left)

	# the ink rides the flow plus what the rings and the drain add (a push the projection
	# would cancel, so the flow itself never takes it); its rows rebuild the ink once
	# every row has read the old one
	def carry_ink(self, t, step):
		global MS_FU, MS_FV, MS_RUN
		self.radiate(t, step)
		# copies, as the original's assignment does: the push below must not reach u and v
		MS_FU = self.u[:]
		MS_FV = self.v[:]
		fu = MS_FU
		fv = MS_FV
		radial = self.radial
		away_r = self.away_r
		away_x = self.away_x
		away_y = self.away_y
		cn = self.cw * self.ch
		for i in range(cn):
			out = radial[away_r[i]]
			fu[i] += away_x[i] * out
			fv[i] += away_y[i] * out
		MS_RUN = step * 2.0
		rows = [maelstrom_ink_row(r) for r in MS_INK_ROWS]
		ink = MS_INK
		ink.clear()
		for r in range(len(rows)):
			ink.extend(rows[r])

	# the wave equation on the ink's grid, in steps short enough that a wave crosses less
	# than a cell in one: each cell is pulled toward the mean of its four neighbours and
	# its speed toward theirs (which calms the chop and leaves the swell); a wall cell
	# copies its inner neighbour, so waves reflect
	def ripple(self, step):
		hgt = self.hw
		vel = self.hv
		gw = self.dw
		gh = self.dh
		wave_speed = self.wave_speed
		subs = int(step * wave_speed / 0.6) + 1
		part = step / subs
		pull = wave_speed * wave_speed * part
		calm = self.wave_drag * part
		keep = 1.0 - 0.8 * part
		last = (gh - 1) * gw
		for sub in range(subs):
			for y in range(1, gh - 1):
				i = y * gw + 1
				for x in range(1, gw - 1):
					here = hgt[i]
					speed = vel[i]
					vel[i] = (speed + pull * (hgt[i - 1] + hgt[i + 1] + hgt[i - gw] + hgt[i + gw] - 4.0 * here)
						+ calm * (vel[i - 1] + vel[i + 1] + vel[i - gw] + vel[i + gw] - 4.0 * speed)) * keep
					i += 1
			for y2 in range(1, gh - 1):
				j = y2 * gw + 1
				for x2 in range(1, gw - 1):
					hgt[j] += vel[j] * part
					j += 1
			for x3 in range(gw):
				hgt[x3] = hgt[x3 + gw]
				hgt[last + x3] = hgt[last - gw + x3]
			for y3 in range(gh):
				r = y3 * gw
				hgt[r] = hgt[r + 1]
				hgt[r + gw - 1] = hgt[r + gw - 2]

	# something lands at ink cell (x, y): a crater `deep` at its middle with a raised rim,
	# which between them hold as much water as the flat they replace
	def splash(self, x, y, deep, rad):
		hgt = self.hw
		dw = self.dw
		r2 = rad * rad
		x0 = max(1, int(x - rad))
		x1 = min(dw - 2, int(x + rad) + 1)
		y0 = max(1, int(y - rad))
		y1 = min(self.dh - 2, int(y + rad) + 1)
		for py in range(y0, y1 + 1):
			ey = py - y
			for px in range(x0, x1 + 1):
				ex = px - x
				q = (ex * ex + ey * ey) / r2
				if q >= 1.0:
					continue
				hgt[py * dw + px] -= deep * (1.0 - q) * (1.0 - q) * (1.0 - 4.0 * q)

	# a disc of the surface about ink cell (x, y) is thrown upward (or, negative, pressed
	# down), fastest at its middle, its rim going the other way by as much water
	def bump(self, x, y, speed, rad):
		vel = self.hv
		dw = self.dw
		r2 = rad * rad
		x0 = max(1, int(x - rad))
		x1 = min(dw - 2, int(x + rad) + 1)
		y0 = max(1, int(y - rad))
		y1 = min(self.dh - 2, int(y + rad) + 1)
		for py in range(y0, y1 + 1):
			ey = py - y
			for px in range(x0, x1 + 1):
				ex = px - x
				q = (ex * ex + ey * ey) / r2
				if q >= 1.0:
					continue
				vel[py * dw + px] += speed * (1.0 - q) * (1.0 - q) * (1.0 - 4.0 * q)

	def drop(self, kind, size, entry, x, y, z, vx, vy, vz):
		i = self.drop_next
		self.drop_next = (self.drop_next + 1) % len(self.drop_kind)
		self.drop_kind[i] = kind
		self.drop_size[i] = size
		self.drop_entry[i] = entry
		self.drop_x[i] = x
		self.drop_y[i] = y
		self.drop_z[i] = z
		self.drop_vx[i] = vx
		self.drop_vy[i] = vy
		self.drop_vz[i] = vz

	# the bright part of the tube the ink at ink cell (x, y) lies in
	def tube_at(self, x, y):
		ink = int(MS_INK[int(y) * self.dw + int(x)])
		return (((ink + self.traded) & 127) // 16) * 16 + 5

	# a blob let go over ink cell (x, y) so that it lands `fall_lead` from now
	def let_fall(self, x, y, size):
		self.blob_tube = (self.blob_tube + 3) % 8
		fall_lead = self.fall_lead
		from_ = self.fall_speed * fall_lead + 0.5 * self.gravity * fall_lead * fall_lead
		self.drop(2, size, self.blob_tube * 16 + 5, x, y, from_, 0.0, 0.0, 0.0 - self.fall_speed)

	# `count` drops of spray thrown up and outward from a ring about ink cell (x, y)
	def crown(self, x, y, count, rad, up, entry):
		rng = self.rng
		unit = self.unit
		from_ = MS_Z[int(y) * self.dw + int(x)] + 0.6
		for j in range(count):
			a = rng.nextf() * 6.2831853
			out = (5.0 + 9.0 * rng.nextf()) * unit
			ca = math.cos(a)
			sa = math.sin(a)
			self.drop(0, 0.0, entry, x + ca * rad, y + sa * rad, from_, ca * out, sa * out, up * (0.55 + 0.6 * rng.nextf()))

	# what the cue throws at the water: rain over the opening, a blob let go ahead of
	# every kick (anywhere at first, then over the pouring nozzles in turn), the hits'
	# own blobs, a spout from a drawing nozzle on each snare, and spray wherever a
	# wave climbs a wall
	def throw_water(self, before, t, step):
		cx = MS_CX
		cy = MS_CY
		rng = self.rng
		unit = self.unit
		dw = self.dw
		dh = self.dh
		t_pair = self.t_pair
		t_pool = self.t_pool
		t_drain = self.t_drain
		fall_lead = self.fall_lead
		self.rain_due += (7.0 if t < t_pair else (1.5 if t < t_drain else 0.0)) * step
		while self.rain_due >= 1.0:
			self.rain_due -= 1.0
			rx = 2.0 + rng.nextf() * (dw - 5.0)
			ry = 2.0 + rng.nextf() * (dh - 5.0)
			self.drop(1, 0.0, self.tube_at(rx, ry), rx, ry, 34.0 * unit, 0.0, 0.0, -25.0 * unit)
		lands = t + fall_lead
		ahead = self.kick_at(lands)
		scripted = abs(lands - t_pair) < 0.25 or abs(lands - t_pool) < 0.25 or abs(lands - t_drain) < 0.25
		if ahead > self.kick_ahead_before + 0.3 and not scripted and lands < t_drain + MUSIC_BAR:
			if lands < t_pair:
				self.let_fall(cx + (rng.nextf() - 0.5) * dw * 0.7, cy + (rng.nextf() - 0.5) * dh * 0.7, 2.2 * unit)
			else:
				reach = 1.0 - 0.5 * self.smooth01((lands - t_drain) / (MUSIC_BAR * 1.5))
				a = self.orbit + self.turn * 0.55 * fall_lead + self.blob_turn * 2.0943951
				self.blob_turn = (self.blob_turn + 1) % 3
				self.let_fall(cx + math.cos(a) * dw * 0.37 * reach, cy + math.sin(a) * dh * 0.37 * reach, 2.2 * unit)
		self.kick_ahead_before = ahead
		pair_falls = t_pair - fall_lead
		if before < pair_falls and t >= pair_falls:
			self.let_fall(cx - dw * 0.23, cy, 3.0 * unit)
			self.let_fall(cx + dw * 0.23, cy, 3.0 * unit)
		pool_falls = t_pool - fall_lead
		if before < pool_falls and t >= pool_falls:
			self.let_fall(cx, cy, 4.5 * unit)
		snare = self.snare_at(t)
		if t >= t_pair and t < t_drain + MUSIC_BAR and snare > self.snare_before + 0.3:
			reach2 = 1.0 - 0.5 * self.smooth01((t - t_drain) / (MUSIC_BAR * 1.5))
			a2 = self.orbit + (1 + 2 * self.spout_turn) * 1.0471976
			self.spout_turn = (self.spout_turn + 1) % 3
			sx = cx + math.cos(a2) * dw * 0.37 * reach2
			sy = cy + math.sin(a2) * dh * 0.37 * reach2
			self.bump(sx, sy, 50.0 * unit, 5.0 * unit)
			self.crown(sx, sy, 5, 1.5 * unit, 60.0 * unit, self.tube_at(sx, sy))
		self.snare_before = snare
		hw = self.hw
		hv = self.hv
		for s in range(10):
			side = rng.next(4)
			along = rng.nextf()
			wx = 2.0 + along * (dw - 5.0)
			wy = 2.0 + along * (dh - 5.0)
			in_x = 0.0
			in_y = 0.0
			if side == 0:
				wy = 2.0
				in_y = 1.0
			elif side == 1:
				wy = dh - 3.0
				in_y = -1.0
			elif side == 2:
				wx = 2.0
				in_x = 1.0
			else:
				wx = dw - 3.0
				in_x = -1.0
			k = int(wy) * dw + int(wx)
			if hw[k] > 3.0 * unit and hv[k] > 0.0:
				self.drop(0, 0.0, self.tube_at(wx, wy), wx, wy, MS_Z[k] + 0.6, in_x * 5.0 * unit, in_y * 5.0 * unit, 30.0 * unit + 0.5 * hv[k])

	# everything in the air falls; what meets the water on its way down lands, and what
	# leaves the tank falls past it to the floor
	def fall(self, step):
		pull = self.gravity * step
		dw = self.dw
		xhi = dw - 2.0
		yhi = self.dh - 2.0
		bed = 0.0 - MS_DEPTH
		z_at = MS_Z
		drop_kind = self.drop_kind
		drop_x = self.drop_x
		drop_y = self.drop_y
		drop_z = self.drop_z
		drop_vx = self.drop_vx
		drop_vy = self.drop_vy
		drop_vz = self.drop_vz
		n = len(drop_kind)
		for i in range(n):
			kind = drop_kind[i]
			if kind < 0:
				continue
			drop_vz[i] -= pull
			x = drop_x[i] + drop_vx[i] * step
			y = drop_y[i] + drop_vy[i] * step
			z = drop_z[i] + drop_vz[i] * step
			drop_x[i] = x
			drop_y[i] = y
			drop_z[i] = z
			if x < 1.0 or x > xhi or y < 1.0 or y > yhi:
				if z < bed:
					drop_kind[i] = -1
				continue
			if drop_vz[i] > 0.0 or z > z_at[int(y) * dw + int(x)]:
				continue
			drop_kind[i] = -1
			self.land(kind, self.drop_size[i], self.drop_entry[i], x, y)

	# spray makes a ripple, rain a small crater and a couple of drops, a blob a crater
	# to its size, a crown of spray and its ink
	def land(self, kind, size, entry, x, y):
		unit = self.unit
		if kind == 0:
			self.splash(x, y, 0.4 * unit, max(3.0 * unit, 2.5))
			return
		if kind == 1:
			self.splash(x, y, 2.0 * unit, max(4.0 * unit, 3.0))
			self.crown(x, y, 2, 1.0 * unit, 26.0 * unit, entry)
			return
		big = size / (2.2 * unit)
		self.splash(x, y, 8.0 * unit * big, 5.5 * unit * big)
		self.pour(x, y, 36.0 * big, 4.5 * unit * big)
		self.crown(x, y, int(8.0 * big * big), 2.4 * unit * big, 46.0 * unit * math.sqrt(big), entry)

	# the camera swings about the tank's long side, framing it whole from wherever it
	# stands: it punches in on the kick and the hits, shakes with a hit, and over the
	# drain climbs and dives at the eye
	def place_camera(self, t):
		global MS_FOCAL, MS_HORIZON, MS_CAM_H, MS_FACE_X, MS_FACE_Y, MS_CAM_X, MS_CAM_Y, MS_GLINT_X, MS_GLINT_Y
		global MS_GRID_FADE, MS_GRID_GAIN
		dw = self.dw
		dh = self.dh
		w = self.w
		h = self.h
		flash = self.hit_at(t)
		dive = self.smooth01((t - self.t_drain) / (MUSIC_BAR * 2.0))
		yaw = 1.5707963 + 0.36 * math.sin(self.cam_yaw)
		fx = math.cos(yaw)
		fy = math.sin(yaw)
		half_w = dw * 0.5
		half_h = dh * 0.5
		dist = 0.9 * math.sqrt(dw * dw + dh * dh * 1.0)
		along_a = abs(half_w * fx + half_h * fy)
		aside_a = abs(half_h * fx - half_w * fy)
		along_b = abs(half_w * fx - half_h * fy)
		aside_b = abs(half_h * fx + half_w * fy)
		along = along_a if along_a > along_b else along_b
		near = dist - along
		far = dist + along
		fit_a = (dist - along_a) / (aside_a + 0.001)
		fit_b = (dist - along_b) / (aside_b + 0.001)
		camh = dist * (0.5 + 0.5 * dive)
		wide = 0.48 * w * (fit_a if fit_a < fit_b else fit_b)
		tall = 0.78 * h / ((camh + MS_DEPTH) / near - camh / far)
		whole = wide if wide < tall else tall
		focal = whole * (1.0 + 1.6 * dive)
		hz_whole = 0.61 * h - 0.5 * whole * ((camh + MS_DEPTH) / near + camh / far)
		hz_eye = 0.5 * h - camh * focal / dist
		hz = hz_whole + (hz_eye - hz_whole) * dive
		punch = 1.0 + 0.035 * self.kick_at(t) + 0.08 * flash
		mid = h * 0.5
		MS_FOCAL = focal * punch
		MS_HORIZON = mid - (mid - hz) * punch + math.sin(t * 53.0) * flash * flash * h * 0.02
		MS_CAM_H = camh
		MS_FACE_X = fx
		MS_FACE_Y = fy
		MS_CAM_X = dw * 0.5 - fx * dist
		MS_CAM_Y = dh * 0.5 - fy * dist
		MS_GLINT_X = 0.5 * fx
		MS_GLINT_Y = 0.5 * fy
		closing = self.smooth01((t - self.t_drain - MUSIC_BAR * 0.5) / (MUSIC_BAR * 1.5))
		reach = far * 1.5 - near * 0.8
		MS_GRID_FADE = 7.0 / reach
		MS_GRID_GAIN = (7.0 + near * 0.8 * MS_GRID_FADE + 1.5 * self.kick_at(t)) * (1.0 - closing)

	# what the picture reads: the waves with the vortices' dimples and the drain's funnel
	# pressed into them, the camera, and the surface's light from its slopes
	def shape_surface(self, t):
		global MS_LIGHT_X, MS_LIGHT_Y, MS_GLINT_NARROW, MS_GLINT, MS_CREST
		self.place_camera(t)
		unit = self.unit
		pool = self.smooth01((t - self.t_pool) / MUSIC_BEAT)
		pair = self.smooth01((t - self.t_pair) / MUSIC_BEAT) * (1.0 - pool)
		drain = self.smooth01((t - self.t_drain) / (MUSIC_BAR * 1.5))
		pair_deep = 5.0 * unit * pair
		pool_deep = 6.0 * unit * pool
		drain_deep = 20.0 * unit * drain
		z = MS_Z
		hgt = self.hw
		pair_dish = self.dish_pair
		pool_dish = self.dish_pool
		dn = self.dw * self.dh
		for i in range(dn):
			f = pool_dish[i]
			z[i] = hgt[i] - pair_deep * pair_dish[i] - (pool_deep + drain_deep * f) * f
		MS_LIGHT_X = 1.4
		MS_LIGHT_Y = 1.9
		MS_GLINT_NARROW = 1.0 / (0.42 * 0.42)
		MS_GLINT = 7.0
		MS_CREST = 0.4 / unit
		rows = [maelstrom_light_row(r) for r in MS_INK_ROWS]
		lit = MS_LIT
		lit.clear()
		for r in range(len(rows)):
			lit.extend(rows[r])

	# what is in the air, over the finished picture: spray and rain as short streaks, blobs as discs
	def draw_drops(self, grid):
		fx = MS_FACE_X
		fy = MS_FACE_Y
		camx = MS_CAM_X
		camy = MS_CAM_Y
		camh = MS_CAM_H
		focal = MS_FOCAL
		hz = MS_HORIZON
		ww = self.w
		hh = self.h
		mid = ww * 0.5
		spray = MS_TUBES + 11 * 128
		blob = MS_TUBES + 14 * 128
		drop_kind = self.drop_kind
		drop_x = self.drop_x
		drop_y = self.drop_y
		drop_z = self.drop_z
		drop_vz = self.drop_vz
		drop_size = self.drop_size
		drop_entry = self.drop_entry
		n = len(drop_kind)
		for i in range(n):
			if drop_kind[i] < 0:
				continue
			ex = drop_x[i] - camx
			ey = drop_y[i] - camy
			depth = ex * fx + ey * fy
			if depth < 4.0:
				continue
			scale = focal / depth
			px = mid + (ey * fx - ex * fy) * scale
			py = hz + (camh - drop_z[i]) * scale
			rad = drop_size[i] * scale
			if rad < 0.9:
				tail = py + drop_vz[i] * 0.03 * scale
				top = py if py < tail else tail
				bottom = tail if py < tail else py
				if px < 0.0 or px >= ww or bottom < 0.0 or top >= hh:
					continue
				sx = int(px)
				ya = 0 if top < 0.0 else int(top)
				yb = hh - 1 if bottom >= hh else int(bottom)
				if yb > ya + 3:
					yb = ya + 3
				pix = spray + drop_entry[i]
				for y in range(ya, yb + 1):
					grid[y * ww + sx] = pix
				continue
			reach = int(rad + 1.0)
			cx = int(px + 1000.0) - 1000
			cy = int(py + 1000.0) - 1000
			r2 = rad * rad + 0.3
			core = blob + drop_entry[i]
			rim = spray + drop_entry[i]
			for oy in range(0 - reach, reach + 1):
				y2 = cy + oy
				if y2 < 0 or y2 >= hh:
					continue
				for ox in range(0 - reach, reach + 1):
					x2 = cx + ox
					q = float(ox * ox + oy * oy)
					if x2 < 0 or x2 >= ww or q > r2:
						continue
					if q * 2.5 < r2:
						grid[y2 * ww + x2] = core
					else:
						grid[y2 * ww + x2] = rim

	def render(self, grid):
		global MS_W, MS_H, MS_TUBES, MS_FLOOR, MS_PHASE
		# the shocks flash the picture negative
		since_pool = self.tt - self.t_pool
		since_drain = self.tt - self.t_drain
		negative = (since_pool >= 0.0 and since_pool < 0.1) or (since_drain >= 0.0 and since_drain < 0.1)
		ww = self.w
		hh = self.h
		MS_W = ww
		MS_H = hh
		MS_TUBES = self.tubes + 2048 if negative else self.tubes
		MS_FLOOR = self.floor_at + 9 if negative else self.floor_at
		MS_PHASE = self.traded + int(3.5 * self.kick_at(self.tt))
		cols = [maelstrom_column(c) for c in MS_COLS]
		n = ww * hh
		if len(grid) != n:
			del grid[n:]
			grid.extend([0] * (n - len(grid)))
		for x in range(ww):
			col = cols[x]
			gi = x
			for y in range(hh):
				grid[gi] = col[y]
				gi += ww
		self.draw_drops(grid)


SCENES[10] = Maelstrom
