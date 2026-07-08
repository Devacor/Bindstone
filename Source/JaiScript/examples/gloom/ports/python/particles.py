"""Particle pool: 288 fixed slots of [kind, x, y, z, vx, vy, vz, life],
round-robin cursor overwrite, stepped in slot order by pure.gloom_particle."""

from math import cos, sin

import state
from pure import gloom_particle
from util import rgb_idx

PART_MAX = 288


class ParticlePool:
	__slots__ = ("pool", "cursor")

	def __init__(self):
		self.pool = []
		self.cursor = 0
		self.reset()

	def reset(self):
		self.pool = [[0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0] for _ in range(PART_MAX)]
		self.cursor = 0

	def spawn(self, kind, x, y, z, vx, vy, vz, life):
		p = self.pool[self.cursor]
		p[0] = kind
		p[1] = x
		p[2] = y
		p[3] = z
		p[4] = vx
		p[5] = vy
		p[6] = vz
		p[7] = life
		self.cursor = (self.cursor + 1) % PART_MAX

	def update(self):
		step = gloom_particle
		for p in self.pool:
			step(p)


# ------------------------------------------------------------- burst kit ----
# All velocity spreads come from RNG (the seeded rng): deterministic, and CALL
# ORDER is part of the conformance contract.
def rnd_spread(scale):
	return (state.RNG.nextf() - 0.5) * 2.0 * scale


def fx_muzzle(x, y, dirx, diry):
	fx = state.G.fx
	rng = state.RNG
	mx = x + dirx * 0.5
	my = y + diry * 0.5
	fx.spawn(5, mx, my, 0.52, dirx * 2.0, diry * 2.0, 0.2, 3)
	for _ in range(3):
		fx.spawn(1, mx, my, 0.5, dirx * 3.0 + rnd_spread(1.6), diry * 3.0 + rnd_spread(1.6), 0.6 + rng.nextf(), 6)
	fx.spawn(4, mx, my, 0.55, dirx * 0.6, diry * 0.6, 0.3, 16)


def fx_wall_hit(x, y):
	fx = state.G.fx
	rng = state.RNG
	for _ in range(5):
		fx.spawn(1, x, y, 0.45 + rng.nextf() * 0.2, rnd_spread(2.2), rnd_spread(2.2), 0.8 + rng.nextf() * 1.4, 8)
	fx.spawn(4, x, y, 0.5, rnd_spread(0.3), rnd_spread(0.3), 0.4, 14)


def fx_blood(x, y, z, n):
	fx = state.G.fx
	rng = state.RNG
	for _ in range(n):
		fx.spawn(2, x, y, z + rng.nextf() * 0.25, rnd_spread(1.8), rnd_spread(1.8), 0.5 + rng.nextf() * 1.6, 12)


def fx_gibs(x, y, n):
	"""death burst: meat + smoke; big enemies gib harder"""
	fx = state.G.fx
	rng = state.RNG
	for _ in range(n):
		fx.spawn(3, x, y, 0.35 + rng.nextf() * 0.3, rnd_spread(2.6), rnd_spread(2.6), 1.2 + rng.nextf() * 2.4, 26)
	for _ in range(3):
		fx.spawn(4, x, y, 0.4, rnd_spread(0.5), rnd_spread(0.5), 0.5, 20)


def fx_explosion(x, y):
	fx = state.G.fx
	rng = state.RNG
	fx.spawn(5, x, y, 0.5, 0.0, 0.0, 0.0, 5)
	for _ in range(10):
		a = rng.nextf() * 6.2831853
		sp = 1.5 + rng.nextf() * 3.0
		fx.spawn(1, x, y, 0.3 + rng.nextf() * 0.5, cos(a) * sp, sin(a) * sp, 1.0 + rng.nextf() * 2.0, 11)
	for _ in range(5):
		fx.spawn(4, x, y, 0.4, rnd_spread(0.9), rnd_spread(0.9), 0.6 + rng.nextf(), 24)
	for _ in range(6):
		fx.spawn(6, x, y, 0.2 + rng.nextf() * 0.6, rnd_spread(1.4), rnd_spread(1.4), 0.5, 20)


def fx_door_puff(x, y):
	fx = state.G.fx
	rng = state.RNG
	for _ in range(4):
		fx.spawn(4, x, y, 0.3 + rng.nextf() * 0.5, rnd_spread(0.7), rnd_spread(0.7), 0.3, 18)


def fx_secret_glitter(x, y):
	fx = state.G.fx
	rng = state.RNG
	for _ in range(8):
		fx.spawn(6, x, y, 0.2 + rng.nextf() * 0.8, rnd_spread(1.2), rnd_spread(1.2), 0.6, 24)


# -------------------------------------------------------------- colors ------
# per-kind life-phase ramps (render reads PART_COLS[kind * 4 + phase])
PART_COLS = []

_RAMPS = (
	((0, 0, 0), (0, 0, 0), (0, 0, 0), (0, 0, 0)),                          # 0 dead (unused)
	((255, 240, 160), (255, 200, 90), (230, 130, 50), (140, 70, 30)),      # 1 spark
	((210, 40, 30), (180, 34, 26), (140, 26, 20), (95, 18, 14)),           # 2 blood
	((200, 60, 44), (170, 46, 34), (130, 34, 26), (90, 24, 18)),           # 3 gib
	((130, 126, 122), (104, 102, 100), (80, 79, 78), (56, 56, 57)),        # 4 smoke
	((255, 252, 220), (255, 236, 160), (255, 190, 90), (200, 120, 50)),    # 5 flash
	((150, 255, 170), (110, 235, 200), (90, 190, 250), (80, 120, 220)),    # 6 hexmote
)


def build_particle_colors():
	PART_COLS[:] = [rgb_idx(r, g, b) for ramp in _RAMPS for (r, g, b) in ramp]
