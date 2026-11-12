"""The showcase look's light (lights.jai): a grid of light texels, LIGHT_SUB to
a tile edge, each holding a light variant (a channel and a level, 0 unlit).
Ceiling lamps and the glowing fixtures are baked when a floor loads or a door
opens; the muzzle flash, projectiles and blasts are stamped over the bake every
frame. Light flows through open tiles only, so it rounds corners and stops at
walls."""

from math import sqrt

import state
from tune import LAMP_POWER, LAMP_RADIUS, LAMP_SPACING
from world import tile_at

LIGHT_SUB = 4
LIGHT_LEVELS = 4
LIGHT_LAMP = 0
LIGHT_FIRE = 1
LIGHT_HEX = 2
LIGHT_ACID = 3
LIGHT_RED = 4
LIGHT_BLUE = 5
LIGHT_CHANNELS = 6
LIGHT_VARIANTS = 25         # unlit + channels x levels

# ordered dither over the light texels: light falls off in a fine stipple, not in rings
BAYER4 = (0.0, 0.5, 0.125, 0.625, 0.75, 0.25, 0.875, 0.375, 0.1875, 0.6875, 0.0625, 0.5625, 0.9375,
	0.4375, 0.8125, 0.3125)

NEIGHBORS = ((1, 0), (-1, 0), (0, 1), (0, -1))


def light_level(variant):
	return 0 if variant == 0 else (variant - 1) % LIGHT_LEVELS + 1


# after the constants: look.py imports them back from here
import look  # noqa: E402


class LightGrid:
	__slots__ = ("gw", "gh", "grid", "lamps", "baked", "baked_gen", "seen", "stamp_id")

	def __init__(self):
		self.gw = 0                 # texels across
		self.gh = 0
		self.grid = []              # this frame's light
		self.lamps = []             # per tile: a lamp hangs over it
		self.baked = []
		self.baked_gen = -1
		self.seen = []              # per tile: the stamp that last reached it
		self.stamp_id = 0

	def load(self):
		"""a floor's lamps: open tiles, LAMP_SPACING apart, picked in a fixed order from a hash"""
		g = state.G
		mw = g.mw
		mh = g.mh
		self.gw = mw * LIGHT_SUB
		self.gh = mh * LIGHT_SUB
		self.lamps = [False] * (mw * mh)
		self.seen = [0] * (mw * mh)
		tiles = g.tiles
		noise = look.texel_noise
		for ty in range(1, mh - 1):
			for tx in range(1, mw - 1):
				if tiles[ty * mw + tx] != 0 or noise(11, tx, ty) % 3 != 0:
					continue
				if not self.lamp_near(tx, ty):
					self.lamps[ty * mw + tx] = True
		self.baked_gen = -1

	def frame(self, sources):
		"""the bake, redone when a door or a secret opens, then this frame's lights"""
		g = state.G
		if self.baked_gen != g.mapgen:
			self.bake()
			self.baked_gen = g.mapgen
		grid = list(self.baked)
		self.grid = grid
		for s in sources:
			self.stamp(grid, s[0], s[1], s[2], s[3], s[4])

	def at(self, x, y):
		gx = int(x * LIGHT_SUB)
		gy = int(y * LIGHT_SUB)
		if gx < 0 or gy < 0 or gx >= self.gw or gy >= self.gh:
			return 0
		return self.grid[gy * self.gw + gx]

	def lamp_near(self, tx, ty):
		g = state.G
		mw = g.mw
		lamps = self.lamps
		for y in range(max(0, ty - LAMP_SPACING + 1), min(g.mh, ty + LAMP_SPACING)):
			for x in range(max(0, tx - LAMP_SPACING + 1), min(mw, tx + LAMP_SPACING)):
				if lamps[y * mw + x]:
					return True
		return False

	def bake(self):
		g = state.G
		mw = g.mw
		baked = [0] * (self.gw * self.gh)
		self.baked = baked
		tiles = g.tiles
		lamps = self.lamps
		for ty in range(g.mh):
			for tx in range(mw):
				t = tiles[ty * mw + tx]
				if lamps[ty * mw + tx]:
					self.stamp(baked, tx + 0.5, ty + 0.5, LIGHT_LAMP, LAMP_RADIUS, LAMP_POWER)
				channel = LIGHT_ACID if t == 6 else (LIGHT_RED if t == 8 else (LIGHT_BLUE if t == 9 else -1))
				if channel < 0:
					continue
				for sx, sy in NEIGHBORS:
					if tile_at(tx + sx, ty + sy) != 0:
						continue
					self.stamp(baked, tx + 0.5 + sx * 0.55, ty + 0.5 + sy * 0.55, channel, 2.4, 0.75)

	def stamp(self, into, lx, ly, channel, radius, power):
		"""light from (lx, ly) over the open tiles a walk of the radius reaches; a
		texel keeps the brighter of what it had and what this light gives it"""
		g = state.G
		mw = g.mw
		sx = int(lx)
		sy = int(ly)
		if tile_at(sx, sy) != 0:
			return
		stamp_id = self.stamp_id + 1
		self.stamp_id = stamp_id
		seen = self.seen
		gw = self.gw
		bayer = BAYER4
		steps = int(radius) + 1
		queue = [sy * mw + sx]
		seen[sy * mw + sx] = stamp_id
		head = 0
		inv_r = 1.0 / radius
		base = 1 + channel * LIGHT_LEVELS
		while head < len(queue):
			tile = queue[head]
			head += 1
			tx = tile % mw
			ty = tile // mw
			for sv in range(LIGHT_SUB):
				wy = ty + (sv + 0.5) / LIGHT_SUB - ly
				row = (ty * LIGHT_SUB + sv) * gw + tx * LIGHT_SUB
				for su in range(LIGHT_SUB):
					wx = tx + (su + 0.5) / LIGHT_SUB - lx
					d = sqrt(wx * wx + wy * wy) * inv_r
					if d >= 1.0:
						continue
					f = power * (1.0 - d) * (1.0 - d * 0.5)
					level = int(f * LIGHT_LEVELS + bayer[((ty * LIGHT_SUB + sv) & 3) * 4 + ((tx * LIGHT_SUB + su) & 3)])
					if level <= 0:
						continue
					if level > LIGHT_LEVELS:
						level = LIGHT_LEVELS
					i = row + su
					old = into[i]
					if level >= (0 if old == 0 else (old - 1) % LIGHT_LEVELS + 1):
						into[i] = base + level - 1
			if abs(tx - sx) + abs(ty - sy) >= steps:
				continue
			for ox, oy in NEIGHBORS:
				nx = tx + ox
				ny = ty + oy
				if tile_at(nx, ny) != 0:
					continue
				nxt = ny * mw + nx
				if seen[nxt] == stamp_id:
					continue
				seen[nxt] = stamp_id
				queue.append(nxt)
