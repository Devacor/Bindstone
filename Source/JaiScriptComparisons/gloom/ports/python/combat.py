"""Combat: line of sight, hitscan weapons, projectiles, explosions, damage."""

from math import cos, floor, sin, sqrt

import hud
import state
from data import AMMO_NAMES, WEAPONS
from particles import fx_blood, fx_explosion, fx_muzzle, fx_wall_hit
from pure import gloom_ray
from util import dist2, trunc, TICK
from world import tile_at, tile_solid


def los_clear(ax, ay, bx, by):
	"""stepped-ray LOS on the tile grid (deterministic float march)"""
	dx = bx - ax
	dy = by - ay
	length = sqrt(dx * dx + dy * dy)
	if length < 0.001:
		return True
	inv = 1.0 / length
	dx *= inv
	dy *= inv
	g = state.G
	mw, mh = g.mw, g.mh
	tiles = g.tiles
	t = 0.15
	while t < length:
		x = floor(ax + dx * t)
		y = floor(ay + dy * t)
		if x < 0 or y < 0 or x >= mw or y >= mh:
			return False
		if tiles[y * mw + x] != 0:
			return False
		t += 0.15
	return True


def wall_dist(ax, ay, dx, dy):
	"""wall distance along an arbitrary (unit) ray — the SAME DDA the renderer uses"""
	return gloom_ray(ax, ay, dx, dy)[0]


def hitscan(ax, ay, dx, dy, lo, hi):
	"""nearest live enemy along a unit ray, cross-checked against the wall"""
	g = state.G
	wd = wall_dist(ax, ay, dx, dy)
	best = None
	best_t = 999.0
	for e in g.enemies:
		if e.alive:
			rx = e.x - ax
			ry = e.y - ay
			t = rx * dx + ry * dy
			if 0.2 <= t <= wd + e.radius and t < best_t:
				perp = rx * dy - ry * dx
				if perp < 0.0:
					perp = 0.0 - perp
				if perp < e.radius + 0.12:
					best = e
					best_t = t
	if best is not None:
		best.hurt(state.RNG.roll(lo, hi))
		return True
	# wall impact garnish just short of the surface
	fx_wall_hit(ax + dx * (wd - 0.08), ay + dy * (wd - 0.08))
	return False


def turret_shot(tx, ty):
	"""turret bolt: instant, accuracy falls off with range"""
	g = state.G
	dx = g.px - tx
	dy = g.py - ty
	d = sqrt(dx * dx + dy * dy)
	if d < 0.001:
		return
	fx_muzzle(tx, ty, dx / d, dy / d)
	hit_pct = max(12, 66 - trunc(d * 5.0))
	if state.RNG.chance(hit_pct) and los_clear(tx, ty, g.px, g.py):
		damage_player(state.RNG.roll(2, 6), "turret fire")
	else:
		# tracer smacks the wall behind you
		wd = wall_dist(tx, ty, dx / d, dy / d) - 0.1
		t = d + 1.5
		if t > wd:
			t = wd
		fx_wall_hit(tx + dx / d * t, ty + dy / d * t)


# ------------------------------------------------------------ projectiles ---
# value records: [kind, x, y, vx, vy, ttl]
#   kind 1 spitter gob | 2 warden hollow fire | 3 player hex bolt
def spawn_shot(kind, ax, ay, tx, ty, speed):
	dx = tx - ax
	dy = ty - ay
	length = sqrt(dx * dx + dy * dy)
	if length < 0.001:
		return
	state.G.shots.append([kind, ax + dx / length * 0.4, ay + dy / length * 0.4,
		dx / length * speed, dy / length * speed, 150])


def spawn_shot_dir(kind, ax, ay, dx, dy, speed):
	state.G.shots.append([kind, ax + dx * 0.45, ay + dy * 0.45, dx * speed, dy * speed, 150])


def update_shots():
	g = state.G
	kept = []
	for s in g.shots:
		kind = s[0]
		x, y = s[1], s[2]
		vx, vy = s[3], s[4]
		ttl = s[5] - 1
		dead = ttl <= 0
		# 3 substeps so fast bolts don't tunnel tile corners
		sub = 0
		while sub < 3 and not dead:
			x += vx * TICK * 0.333333333
			y += vy * TICK * 0.333333333
			if tile_solid(tile_at(floor(x), floor(y))):
				if kind == 3:
					explode_hex(x - vx * 0.02, y - vy * 0.02)
				elif kind == 2:
					explode_fire(x - vx * 0.02, y - vy * 0.02)
				else:
					fx_wall_hit(x - vx * 0.02, y - vy * 0.02)
				dead = True
				break
			if kind == 3:
				# player bolt vs enemies
				for e in g.enemies:
					if e.alive:
						r = e.radius + 0.2
						if dist2(x, y, e.x, e.y) < r * r:
							explode_hex(x, y)
							dead = True
							break
			else:
				# enemy shot vs player
				if dist2(x, y, g.px, g.py) < 0.14:
					if kind == 2:
						explode_fire(x, y)
					else:
						damage_player(state.RNG.roll(4, 9), "a caustic gob")
						fx_blood(x, y, 0.5, 3)
					dead = True
					break
			sub += 1
		if not dead:
			kept.append([kind, x, y, vx, vy, ttl])
	g.shots = kept
	# glow trail (0-based shot index in the (tick + i) % 2 phase rule)
	fx = g.fx
	tick = g.tick
	for i, s2 in enumerate(kept):
		if (tick + i) % 2 == 0:
			k2 = s2[0]
			fx.spawn(6 if k2 == 1 else (1 if k2 == 2 else 6), s2[1], s2[2], 0.5, 0.0, 0.0, 0.0, 4)


def explode_hex(x, y):
	g = state.G
	fx_explosion(x, y)
	g.noise = 20
	for e in g.enemies:
		if e.alive:
			d2 = dist2(x, y, e.x, e.y)
			if d2 < 3.24:                              # radius 1.8
				dmg = trunc(42.0 - sqrt(d2) * 14.0)
				if dmg > 0:
					e.hurt(dmg)
	# standing in your own spell is a choice
	pd2 = dist2(x, y, g.px, g.py)
	if pd2 < 2.25:
		self_dmg = trunc(20.0 - sqrt(pd2) * 10.0)
		if self_dmg > 0:
			damage_player(self_dmg, "your own hex")


def explode_fire(x, y):
	g = state.G
	fx_explosion(x, y)
	pd2 = dist2(x, y, g.px, g.py)
	if pd2 < 2.89:                                 # radius 1.7
		dmg = trunc(26.0 - sqrt(pd2) * 9.0)
		if dmg > 0:
			damage_player(dmg, "hollow fire")


# ------------------------------------------------------------ the player ----
def damage_player(dmg, src):
	g = state.G
	if g.mode != 1 or dmg <= 0:
		return
	if state.HOST_GOD:
		return
	absorbed = min(g.armor, dmg * 2 // 3)
	g.armor -= absorbed
	taken = dmg - absorbed
	g.hp -= taken
	g.face_pain = 12
	g.hurt_flash = 4
	hud.show_msg(f"-{taken} hp ({src})", "91")
	if g.hp <= 0:
		g.hp = 0
		g.mode = 3
		g.mode_t = 0
		g.death_cause = src
		hud.show_msg("you died", "91")


def player_fire():
	g = state.G
	if g.cooldown > 0:
		return
	w = WEAPONS[g.weapon]
	ammo_type = w["ammo"]
	if g.ammo[ammo_type] <= 0:
		hud.show_msg("out of " + AMMO_NAMES[ammo_type] + "S", "93")
		g.cooldown = 8
		return
	g.ammo[ammo_type] -= 1
	g.cooldown = w["cd"]
	g.muzzle = 3
	g.noise = 24
	g.gun_kick = 4
	dirx = cos(g.pang)
	diry = sin(g.pang)
	fx_muzzle(g.px, g.py, dirx, diry)
	if w["kind"] == 1:
		spawn_shot_dir(3, g.px, g.py, dirx, diry, 9.0)
		return
	spread = w["spread"]
	rng = state.RNG
	for _ in range(w["pellets"]):
		a = g.pang + (rng.nextf() - 0.5) * 2.0 * spread
		hitscan(g.px, g.py, cos(a), sin(a), w["lo"], w["hi"])
