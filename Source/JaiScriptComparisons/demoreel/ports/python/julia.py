"""JULIA (scenes/julia.jai): the whole Julia set, morphing in frame. The constant is steered
through its fixed point's multiplier lambda = rho * e^(i theta), c = lambda / 2 - lambda^2 / 4.
Every pixel iterates z and its derivative: the smooth escape count picks the hue, the
distance estimate draws the boundary as a white-hot rim, and the potential's gradient lights
the bands as a relief. Every view is centred on the origin, so a frame computes half its rows
and mirrors them.
"""

import math

from music import MUSIC_BAR, MUSIC_BEAT
from reel import Scene, SCENES

JU_CA = 0.0          # c
JU_CB = 0.0
JU_FX = 0.0          # the fixed point alpha = lambda / 2
JU_FY = 0.0
JU_TRAP = 0.0        # squared radius round alpha inside which an orbit is home; 0 when alpha repels
JU_DRIFT = 0.0       # what an iteration adds to the inside's potential, so the trap's early exit leaves no seam
JU_MAXIT = 40
JU_W = 0
JU_H = 0
JU_UX = 0.0          # the view, centred on the origin: a pixel's step right and down
JU_UY = 0.0
JU_VX = 0.0
JU_VY = 0.0
JU_PIXELS = 1.0      # pixels per plane unit
JU_UNRESOLVED = 0.0  # squared stretch past which a pixel that never escaped is boundary, not inside
JU_LX = 0.0          # the light's direction in the plane
JU_LY = 0.0
JU_PHASE = 0.0       # the palette's turn, outside and inside
JU_INPHASE = 0.0
JU_LIFT = 0.0        # light added on the beat
JU_RIM = 1.0         # the rim's width in pixels
JU_INSIDE = 0
JU_ROWS = []

JU_HUES = 64
JU_LEVELS = 12
JU_INHUES = 32

JU_RHO = 0           # the path's tracks
JU_THETA = 1
JU_DIVE = 2
JU_SPIN = 3


# one row of the top half; each entry packs the pixel and the one half a turn away,
# whose orbit is the same and whose gradient points the other way
def julia_row(y):
	out = []
	push = out.append
	log2 = math.log2
	sqrt = math.sqrt
	w = JU_W
	mi = JU_MAXIT
	ca = JU_CA
	cb = JU_CB
	ux = JU_UX
	uy = JU_UY
	py = y + 0.5 - JU_H * 0.5
	left = 0.5 - w * 0.5
	rx = py * JU_VX + left * ux
	ry = py * JU_VY + left * uy
	lx = JU_LX
	ly = JU_LY
	fx = JU_FX
	fy = JU_FY
	trap = JU_TRAP
	drift = JU_DRIFT
	pixels = JU_PIXELS
	phase = JU_PHASE
	inphase = JU_INPHASE
	lift = JU_LIFT
	rim = JU_RIM
	rimfall = 0.5 / rim
	inside = JU_INSIDE
	unresolved = JU_UNRESOLVED
	hot = (int(mi * 3.5 + phase) & 63) * 12 + 11
	for x in range(w):
		zx = rx + x * ux
		zy = ry + x * uy
		x2 = zx * zx
		y2 = zy * zy
		dx = 1.0
		dy = 0.0
		e2 = 1.0
		k = 0
		# four steps to a test: an escaped orbit's count and distance stay exact however far it runs on
		while k < mi:
			dx, dy = 2.0 * (zx * dx - zy * dy), 2.0 * (zx * dy + zy * dx)
			zy = 2.0 * zx * zy + cb
			zx = x2 - y2 + ca
			x2 = zx * zx
			y2 = zy * zy
			dx, dy = 2.0 * (zx * dx - zy * dy), 2.0 * (zx * dy + zy * dx)
			zy = 2.0 * zx * zy + cb
			zx = x2 - y2 + ca
			x2 = zx * zx
			y2 = zy * zy
			dx, dy = 2.0 * (zx * dx - zy * dy), 2.0 * (zx * dy + zy * dx)
			zy = 2.0 * zx * zy + cb
			zx = x2 - y2 + ca
			x2 = zx * zx
			y2 = zy * zy
			dx, dy = 2.0 * (zx * dx - zy * dy), 2.0 * (zx * dy + zy * dx)
			zy = 2.0 * zx * zy + cb
			zx = x2 - y2 + ca
			x2 = zx * zx
			y2 = zy * zy
			k += 4
			if x2 + y2 >= 64.0:
				break
			ex = zx - fx
			ey = zy - fy
			e2 = ex * ex + ey * ey
			if e2 < trap:
				break
		m2 = x2 + y2
		d2 = dx * dx + dy * dy
		if m2 < 64.0:
			c = hot
			# an orbit still in after stretching its pixel past the whole set sits on the boundary
			if d2 < unresolved:
				band = k * drift - log2(e2 + 0.000001) * 1.7 + inphase
				c = inside + (int(band + 1024.0) & 31)
			push(c * 1025)
			continue
		lg = log2(m2)
		nu = k + 1.0 - log2(lg * 0.5)
		unit = 1.0 / sqrt(m2 * d2 + 1.0e-300)
		# the distance to the set, in pixels: |z| ln|z| / |z'|
		de = m2 * unit * lg * 0.34657 * pixels
		# the potential's gradient runs along z * conj(z')
		nl = ((zx * dx + zy * dy) * lx + (zy * dx - zx * dy) * ly) * unit
		glow = 1.0 / (1.0 + de * 0.3)
		lit = 0.4 + 0.3 * glow + lift + (0.5 - de * rimfall if de < rim else 0.0)
		relief = nl * 0.2
		hue = (int(nu * 3.5 + phase) & 63) * 12
		lv = int((lit + relief) * 11.99)
		lv = 0 if lv < 0 else (11 if lv > 11 else lv)
		lm = int((lit - relief) * 11.99)
		lm = 0 if lm < 0 else (11 if lm > 11 else lm)
		push(hue + lv + (hue + lm) * 1024)
	return out


class Julia(Scene):
	def __init__(self):
		Scene.__init__(self)
		self.tt = 0.0
		# the path, in beats: over key_from..key_to a track glides to key_value. rho and theta are
		# lambda's polar form, dive is octaves of zoom into the origin, spin radians of turn
		self.key_track = []
		self.key_from = []
		self.key_to = []
		self.key_value = []
		self.key_lands = []

	def hit_times(self):
		return [MUSIC_BAR * 3.0, MUSIC_BAR * 5.0, MUSIC_BAR * 8.0, MUSIC_BAR * 9.0]

	def hit_levels(self):
		return [2, 1, 2, 1]

	def length_bars(self):
		return 10

	def glide(self, track, from_, to, value):
		self.key_track.append(track)
		self.key_from.append(float(from_))
		self.key_to.append(float(to))
		self.key_value.append(float(value))
		self.key_lands.append(False)

	# a glide that leaves at full speed and settles: the move a hit throws
	def land(self, track, from_, to, value):
		self.glide(track, from_, to, value)
		self.key_lands[len(self.key_lands) - 1] = True

	def build(self):
		global JU_INSIDE
		self.name = "JULIA"
		self.handle = "one constant, every shape . by the type ladder"
		pal = self.pal
		night = [24.0, 12.0, 66.0]
		stops = [[40.0, 30.0, 150.0], [120.0, 50.0, 210.0], [240.0, 50.0, 150.0], [255.0, 130.0, 50.0],
			[255.0, 220.0, 110.0], [80.0, 230.0, 220.0], [30.0, 120.0, 235.0], [40.0, 30.0, 150.0]]
		ns = len(stops) - 1
		for hq in range(JU_HUES):
			f = hq * ns * 1.0 / JU_HUES
			k = int(f)
			u = f - k
			a = stops[k]
			b = stops[k + 1]
			# a hue's ramp runs night, the hue in full, white hot
			for l in range(JU_LEVELS):
				lit = l / (JU_LEVELS - 1.0)
				sat = 1.0 if lit * 1.8 > 1.0 else lit * 1.8
				wh = (lit - 0.75) * 3.0 if lit > 0.75 else 0.0
				rgb = [0.0, 0.0, 0.0]
				for ch in range(3):
					full = (a[ch] + (b[ch] - a[ch]) * u) * (0.5 + 0.5 * lit)
					v = night[ch] + (full - night[ch]) * sat
					rgb[ch] = v + (255.0 - v) * wh
				pal.add(rgb[0], rgb[1], rgb[2])
		JU_INSIDE = pal.gradient(JU_INHUES + 1, [[6.0, 4.0, 22.0], [60.0, 18.0, 96.0], [10.0, 8.0, 36.0], [14.0, 62.0, 96.0], [6.0, 4.0, 22.0]])

		# a fat set swells four ears by bar 1, trades them for the rabbit's three by bar 2,
		# and through the riser the rabbit starves to the six-armed dendrite past its bulb's tip
		glide = self.glide
		land = self.land
		glide(JU_THETA, 0.0, 2.5, 1.5708)
		glide(JU_RHO, 2.0, 4.0, 1.0625)
		glide(JU_RHO, 4.5, 5.5, 0.96)
		glide(JU_THETA, 5.0, 7.0, 2.0944)
		glide(JU_RHO, 6.5, 8.0, 1.107)
		glide(JU_RHO, 9.0, 12.0, 1.3283)
		glide(JU_THETA, 9.0, 12.0, 2.0866)
		glide(JU_DIVE, 9.0, 12.0, 0.7)
		# the bar 3 hit dives into the junction at its heart and turns; the pull back fattens the
		# arms and slides along the cardioid to San Marco, which the bar 5 accent twists into a dragon
		land(JU_DIVE, 12.0, 15.5, 3.7)
		land(JU_SPIN, 12.0, 20.0, 3.14159265)
		glide(JU_DIVE, 15.5, 17.5, 0.0)
		glide(JU_RHO, 16.5, 18.2, 1.0)
		glide(JU_THETA, 17.7, 19.7, 3.14159265)
		land(JU_RHO, 20.0, 21.5, 1.0723)
		land(JU_THETA, 20.0, 21.5, 3.0068)
		# the basilica on bar 6, a fat set, the Siegel disk on bar 7
		glide(JU_RHO, 22.5, 24.0, 1.236)
		glide(JU_THETA, 22.5, 24.0, 3.14159265)
		glide(JU_RHO, 24.5, 26.0, 0.97)
		glide(JU_THETA, 25.5, 28.0, 3.8832)
		glide(JU_RHO, 26.5, 28.0, 1.0)
		# the riser carries it up the quarter bulb to an eight-armed star; the bar 8 hit dives in
		glide(JU_THETA, 28.5, 30.3, 4.7124)
		glide(JU_RHO, 30.3, 32.0, 1.2679)
		glide(JU_THETA, 31.0, 32.0, 4.6648)
		glide(JU_DIVE, 29.0, 32.0, 0.6)
		land(JU_DIVE, 32.0, 35.5, 3.6)
		land(JU_SPIN, 32.0, 39.0, 6.2831853)
		# bar 9 pulls back and the star fattens to the four-eared set, which holds the frame to the end
		glide(JU_DIVE, 36.0, 38.0, 0.0)
		glide(JU_THETA, 37.0, 39.0, 4.7124)
		glide(JU_RHO, 37.0, 39.0, 1.0625)

	def update(self, t, dt):
		self.tt = t

	def render(self, grid):
		global JU_FX, JU_FY, JU_CA, JU_CB, JU_TRAP, JU_DRIFT, JU_UX, JU_UY, JU_VX, JU_VY, JU_PIXELS
		global JU_UNRESOLVED, JU_LX, JU_LY, JU_PHASE, JU_INPHASE, JU_LIFT, JU_RIM, JU_W, JU_H, JU_ROWS
		sin = math.sin
		cos = math.cos
		log2 = math.log2
		t = self.tt
		w = self.w
		h = self.h
		cue = self.cue
		beats = t / MUSIC_BEAT
		kick = cue.kick_env(t)
		snare = cue.snare_env(t)
		hitv = cue.hit(t)
		track = [0.92, 0.9, 0.0, 0.0]
		key_track = self.key_track
		key_from = self.key_from
		key_to = self.key_to
		key_value = self.key_value
		key_lands = self.key_lands
		for i in range(len(key_from)):
			from_ = key_from[i]
			if beats <= from_:
				continue
			k = 1.0 if beats >= key_to[i] else (beats - from_) / (key_to[i] - from_)
			back = 1.0 - k
			k = 1.0 - back * back * back if key_lands[i] else k * k * (3.0 - 2.0 * k)
			which = key_track[i]
			track[which] += (key_value[i] - track[which]) * k
		rho = track[JU_RHO]
		theta = track[JU_THETA]
		dive = track[JU_DIVE]
		spin = track[JU_SPIN] + 0.07 * sin(t * 0.9)
		lr = rho * cos(theta)
		li = rho * sin(theta)
		JU_FX = lr * 0.5
		JU_FY = li * 0.5
		JU_CA = lr * 0.5 - (lr * lr - li * li) * 0.25
		JU_CB = li * 0.5 - lr * li * 0.5
		# near alpha the map is e -> lambda e + e^2, a contraction while |e| < 1 - rho
		home = (1.0 - rho) * 0.6 if rho < 1.0 else 0.0
		JU_TRAP = home * home
		JU_DRIFT = 3.4 * log2(rho) if rho < 1.0 else 0.0
		# beta = 1 - alpha is the set's far tip: the set spans beta to -beta
		bx = 1.0 - JU_FX
		by = 0.0 - JU_FY
		reach = math.sqrt(bx * bx + by * by)
		half_width = reach * 1.2
		half_width = 1.4 if half_width < 1.4 else half_width
		scale = half_width * 2.0 / w * (1.0 - kick * 0.05) / math.pow(2.0, dive)
		turn = math.atan2(by, bx) + spin - 0.6 * sin(theta)
		cs = cos(turn)
		sn = sin(turn)
		JU_UX = cs * scale
		JU_UY = sn * scale
		JU_VX = 0.0 - sn * scale
		JU_VY = cs * scale
		JU_PIXELS = 1.0 / scale
		# the critical orbit's mean stretch tells a set with an inside (it shrinks) from a dendrite (it grows)
		ox = JU_CA
		oy = JU_CB
		stretch = 0.0
		steps = 0
		while steps < 48 and ox * ox + oy * oy < 64.0:
			stretch += math.log(4.0 * (ox * ox + oy * oy) + 0.000000000001)
			nx = ox * ox - oy * oy + JU_CA
			oy = 2.0 * ox * oy + JU_CB
			ox = nx
			steps += 1
		thin = stretch * 0.5 / steps - 0.06 if steps > 0 else 1.0
		thin = 0.0 if thin < 0.0 else (0.25 if thin > 0.25 else thin)
		JU_UNRESOLVED = JU_PIXELS * JU_PIXELS * math.pow(2.0, 10.0 - 400.0 * thin)
		JU_LX = -0.6 * cs + 0.8 * sn
		JU_LY = -0.6 * sn - 0.8 * cs
		JU_PHASE = 4096.0 + t * 9.0 + cue.chord(t) * 5.0
		JU_INPHASE = t * 4.0
		JU_LIFT = kick * 0.1 + snare * 0.06 + hitv * 0.14
		JU_RIM = 1.1 + kick * 0.5 + hitv * 1.5
		JU_W = w
		JU_H = h
		rows_drawn = (h + 1) // 2
		if len(JU_ROWS) != rows_drawn:
			JU_ROWS = list(range(rows_drawn))
		rows = [julia_row(r) for r in JU_ROWS]
		last = w * h - 1
		idx = 0
		for row in rows:
			for v in row:
				grid[idx] = v & 1023
				grid[last - idx] = v >> 10
				idx += 1


SCENES[2] = Julia
