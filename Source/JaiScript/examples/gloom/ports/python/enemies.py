"""Enemies: one instance per body in G.enemies, and each enemy's BRAIN is a
plain Python generator stored in its own field — kind-specific generator
methods, one `next()` per tick, and all phase state lives in the generator
frame (a timer is `for _ in range(7): yield`). Pain interrupts by discarding
the handle; tick() re-mints it."""

from math import floor, sqrt

import combat
import hud
import sim
import state
from data import ENEMY_DEFS
from particles import fx_blood, fx_explosion, fx_gibs
from util import dist2, TICK
from world import walkable

STATE_DORMANT = 0
STATE_HUNT = 1
STATE_ATTACK = 2
STATE_DYING = 3
STATE_DEAD = 4


class Enemy:
	__slots__ = ("kind", "x", "y", "hp", "alive", "state", "anim", "walk",
		"flash", "stun", "radius", "speed", "brain")

	def __init__(self, kind, x, y):
		d = ENEMY_DEFS[kind]
		self.kind = kind
		self.x = x
		self.y = y
		self.hp = d["hp"]
		self.alive = True
		self.state = STATE_DORMANT
		self.anim = 0           # ticks in current state (drives art frames)
		self.walk = 0           # walk cycle counter (advances only when moving)
		self.flash = 0          # damage flash ticks
		self.stun = 0           # pain stun ticks (brain dropped + re-minted)
		self.radius = d["radius"]
		self.speed = d["speed"]
		self.brain = None       # generator handle in a field: the whole point

	def tick(self):
		"""one sim tick: corpse cools, stun blocks, otherwise the brain runs"""
		self.anim += 1
		if self.flash > 0:
			self.flash -= 1
		if not self.alive:
			return
		if self.stun > 0:
			self.stun -= 1
			return
		br = self.brain
		if br is None or br.gi_frame is None:      # None / exhausted -> re-mint
			br = _BRAINS[self.kind](self)
			self.brain = br
		next(br)

	def player_dist(self):
		g = state.G
		return sqrt(dist2(self.x, self.y, g.px, g.py))

	def sees_player(self):
		if self.player_dist() > 13.0:
			return False
		g = state.G
		return combat.los_clear(self.x, self.y, g.px, g.py)

	def hears_player(self):
		return state.G.noise > 0 and self.player_dist() < 12.0

	def move_toward(self, tx, ty):
		"""slide movement against the tile grid (axes resolved separately)"""
		x, y = self.x, self.y
		dx = tx - x
		dy = ty - y
		length = sqrt(dx * dx + dy * dy)
		if length < 0.0001:
			return
		step = self.speed * TICK
		nx = x + dx / length * step
		ny = y + dy / length * step
		r = self.radius
		spot_free = sim.spot_free
		if spot_free(nx, y, r):
			self.x = nx
			self.walk += 1
		if spot_free(self.x, ny, r):
			self.y = ny
			self.walk += 1

	def hurt(self, dmg):
		if not self.alive:
			return
		self.hp -= dmg
		self.flash = 4
		fx_blood(self.x, self.y, 0.5, min(6, 2 + dmg // 6))
		if self.hp <= 0:
			self.die()
			return
		# pain: drop the brain mid-phase; re-minted next tick = re-telegraph
		rng = state.RNG
		if rng.chance(ENEMY_DEFS[self.kind]["pain"]):
			self.stun = 5 + rng.next(5)
			self.brain = None
			self.state = STATE_HUNT
			self.anim = 0

	def die(self):
		self.alive = False
		self.state = STATE_DYING
		self.anim = 0
		self.brain = None
		g = state.G
		g.kills += 1
		fx_gibs(self.x, self.y, 14 if self.kind == 3 else 7)
		if self.kind == 3:
			fx_explosion(self.x, self.y)
			g.warden_down = True
		hud.show_msg(ENEMY_DEFS[self.kind]["name"] + " destroyed", "92")

	# --------------------------------------------------------- brains -------
	# One yield = one tick. Timers are plain loop counters living in the
	# generator frame; pain interrupts by discarding the handle (tick()).

	def brain_grunt(self):
		"""GRUNT: doze -> roar -> relentless zigzag chase -> lunge bite"""
		g = state.G
		while self.state == STATE_DORMANT:
			if (self.sees_player() and self.player_dist() < 11.0) or self.hears_player():
				break
			yield
		self.state = STATE_HUNT
		self.anim = 0
		for _ in range(7):              # the roar (it commits)
			yield
		zig = 1 if state.RNG.next(2) == 0 else -1
		while True:
			d = self.player_dist()
			if d < 1.3:
				self.state = STATE_ATTACK
				self.anim = 0
				for _ in range(6):      # lunge windup
					yield
				if self.player_dist() < 1.6 and combat.los_clear(self.x, self.y, g.px, g.py):
					combat.damage_player(state.RNG.roll(ENEMY_DEFS[0]["melee_lo"], ENEMY_DEFS[0]["melee_hi"]),
						"a grunt's claws")
				self.state = STATE_HUNT
				self.anim = 0
				for _ in range(5):      # recover
					yield
			else:
				# zigzag pursuit: aim past the player's flank, swapping sides
				if self.anim % 20 == 19:
					zig = -zig
				fx = g.py - self.y
				fy = self.x - g.px
				fl = sqrt(fx * fx + fy * fy)
				if fl < 0.001:
					fl = 1.0
				lean = 0.9 if d > 3.0 else 0.2
				self.move_toward(g.px + fx / fl * lean * zig, g.py + fy / fl * lean * zig)
				yield

	def brain_spitter(self):
		"""SPITTER: keeps its range band, strafes, telegraphs, spits"""
		g = state.G
		while self.state == STATE_DORMANT:
			if (self.sees_player() and self.player_dist() < 12.0) or self.hears_player():
				break
			yield
		self.state = STATE_HUNT
		self.anim = 0
		orbit = 1 if state.RNG.next(2) == 0 else -1
		while True:
			d = self.player_dist()
			los = combat.los_clear(self.x, self.y, g.px, g.py)
			if los and 2.0 < d < 9.5:
				# telegraph glow, then the gob
				self.state = STATE_ATTACK
				self.anim = 0
				for _ in range(9):
					yield
				if combat.los_clear(self.x, self.y, g.px, g.py):
					combat.spawn_shot(1, self.x, self.y, g.px, g.py, 6.5)
				self.state = STATE_HUNT
				self.anim = 0
				# cooldown spent orbiting sideways
				for _ in range(16):
					ox = g.py - self.y
					oy = self.x - g.px
					ol = sqrt(ox * ox + oy * oy)
					if ol < 0.001:
						ol = 1.0
					self.move_toward(self.x + ox / ol * orbit, self.y + oy / ol * orbit)
					yield
				if state.RNG.chance(40):
					orbit = -orbit
			elif d <= 2.0:
				# too close: shove and retreat
				if d < 1.2 and state.RNG.chance(30):
					combat.damage_player(state.RNG.roll(ENEMY_DEFS[1]["melee_lo"], ENEMY_DEFS[1]["melee_hi"]),
						"a spitter's talons")
				self.move_toward(self.x + (self.x - g.px), self.y + (self.y - g.py))
				yield
			else:
				self.move_toward(g.px, g.py)
				yield

	def brain_turret(self):
		"""TURRET: dormant metal until it has line of sight; then 3-round bursts."""
		while True:
			while not (self.sees_player() and self.player_dist() < 11.0):
				self.state = STATE_DORMANT
				yield
			self.state = STATE_HUNT     # waking whir
			self.anim = 0
			for _ in range(8):
				yield
			while self.sees_player() and self.player_dist() < 12.0:
				self.state = STATE_ATTACK
				self.anim = 0
				for _ in range(3):
					combat.turret_shot(self.x, self.y)
					yield
					yield
				self.state = STATE_HUNT
				self.anim = 0
				for _ in range(13):
					yield

	def brain_warden(self):
		"""WARDEN: the landlord. Stalks and lobs hollow fire; under half health
		he goes double-volley and calls the family, exactly once."""
		g = state.G
		enraged = False
		summoned = False
		while self.state == STATE_DORMANT:
			if (self.sees_player() and self.player_dist() < 13.0) or self.hears_player():
				break
			yield
		self.state = STATE_HUNT
		self.anim = 0
		hud.show_msg("THE WARDEN HAS SEEN YOU", "91")
		for _ in range(10):
			yield
		while True:
			if not enraged and self.hp * 2 < ENEMY_DEFS[3]["hp"]:
				enraged = True
				self.speed *= 1.5
				hud.show_msg("The Warden's wounds glow like coals", "91")
			if enraged and not summoned:
				summoned = True
				summon_adds(self.x, self.y)
				hud.show_msg("The Warden howls for his pack", "91")
			d = self.player_dist()
			los = combat.los_clear(self.x, self.y, g.px, g.py)
			if los and d > 2.4:
				self.state = STATE_ATTACK
				self.anim = 0
				for _ in range(8):
					yield
				volleys = 2 if enraged else 1
				for _ in range(volleys):
					if combat.los_clear(self.x, self.y, g.px, g.py):
						combat.spawn_shot(2, self.x, self.y, g.px, g.py, 5.5)
					for _ in range(4):
						yield
				self.state = STATE_HUNT
				self.anim = 0
				rest = 6 if enraged else 12
				for _ in range(rest):
					self.move_toward(g.px, g.py)
					yield
			elif d < 1.7:
				self.state = STATE_ATTACK
				self.anim = 0
				for _ in range(5):
					yield
				if self.player_dist() < 2.0:
					combat.damage_player(state.RNG.roll(ENEMY_DEFS[3]["melee_lo"], ENEMY_DEFS[3]["melee_hi"]),
						"the Warden's fist")
				self.state = STATE_HUNT
				self.anim = 0
				for _ in range(4):
					yield
			else:
				self.move_toward(g.px, g.py)
				yield


_BRAINS = (Enemy.brain_grunt, Enemy.brain_spitter, Enemy.brain_turret, Enemy.brain_warden)


def spawn_enemy(kind, tx, ty):
	g = state.G
	g.enemies.append(Enemy(kind, tx + 0.5, ty + 0.5))
	g.kill_total += 1


def summon_adds(wx, wy):
	"""warden's reinforcements: grunts at the nearest free tiles around him."""
	placed = 0
	for ox, oy in ((2, 0), (-2, 0), (0, 2), (0, -2)):
		if placed >= 2:
			break
		tx = floor(wx) + ox
		ty = floor(wy) + oy
		if walkable(tx, ty):
			spawn_enemy(0, tx, ty)
			fx_explosion(tx + 0.5, ty + 0.5)
			placed += 1


# which art a body renders as, given its state machinery
_ART_BASE = ("grunt", "spit", "tur", "war")


def enemy_art(e):
	name = _ART_BASE[e.kind]
	if e.kind == 2:
		# turret has its own frame set
		if e.alive:
			return "tur_fire" if e.state == STATE_ATTACK else "tur_idle"
		if e.anim < 6:
			return "tur_d0"
		if e.anim < 12:
			return "tur_d1"
		return "tur_dead"
	if e.alive:
		if e.state == STATE_ATTACK:
			return name + "_at"
		if (e.walk // 6) % 2 == 0:
			return name + "_w0"
		return name + "_w1"
	if e.anim < 6:
		return name + "_d0"
	if e.anim < 12:
		return name + "_d1"
	return name + "_dead"
