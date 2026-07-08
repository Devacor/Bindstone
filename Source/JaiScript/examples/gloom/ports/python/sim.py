"""Player simulation: movement with wall sliding, the 'use' action (doors,
secrets, the exit switch), pickups."""

from math import cos, floor, sin, sqrt

import hud
import state
from data import AMMO_MAX, ITEM_DEFS
from particles import fx_door_puff, fx_secret_glitter
from util import dist2, TICK
from world import rebuild_mapkind, tile_at, tile_solid


def spot_free(x, y, r):
	"""circle-vs-grid: all four corners of the actor's square must be on floor"""
	if tile_solid(tile_at(floor(x - r), floor(y - r))):
		return False
	if tile_solid(tile_at(floor(x + r), floor(y - r))):
		return False
	if tile_solid(tile_at(floor(x - r), floor(y + r))):
		return False
	if tile_solid(tile_at(floor(x + r), floor(y + r))):
		return False
	return True


def player_move(inp):
	g = state.G
	turn = 0.0
	if inp.tl:
		turn -= 1.0
	if inp.tr:
		turn += 1.0
	g.pang += turn * 2.7 * TICK
	while g.pang > 6.2831853:
		g.pang -= 6.2831853
	while g.pang < 0.0:
		g.pang += 6.2831853

	dirx = cos(g.pang)
	diry = sin(g.pang)
	wx = 0.0
	wy = 0.0
	if inp.fwd:
		wx += dirx
		wy += diry
	if inp.back:
		wx -= dirx
		wy -= diry
	if inp.sl:
		wx += diry
		wy -= dirx
	if inp.sr:
		wx -= diry
		wy += dirx
	wl = sqrt(wx * wx + wy * wy)
	if wl > 0.001:
		run = inp.run
		speed = (5.0 if run else 3.4) * TICK
		wx = wx / wl * speed
		wy = wy / wl * speed
		nx = g.px + wx
		ny = g.py + wy
		moved = False
		if spot_free(nx, g.py, 0.26):
			g.px = nx
			moved = True
		if spot_free(g.px, ny, 0.26):
			g.py = ny
			moved = True
		if moved:
			g.bob += 0.30 if run else 0.22
			check_pickups()


# ------------------------------------------------------------ 'use' ---------
def player_use():
	"""probe a few steps down the facing for the first non-floor tile"""
	g = state.G
	dirx = cos(g.pang)
	diry = sin(g.pang)
	for p in (0.5, 0.9, 1.3):
		tx = floor(g.px + dirx * p)
		ty = floor(g.py + diry * p)
		t = tile_at(tx, ty)
		if t != 0:
			use_tile(tx, ty, t)
			return
	hud.show_msg("nothing to use", "90")


def use_tile(tx, ty, t):
	g = state.G
	if t == 5:
		open_door(tx, ty, "the door grinds open")
		return
	if t == 8:
		if g.key_r:
			open_door(tx, ty, "RED lock released")
		else:
			hud.show_msg("you need the RED keycard", "91")
		return
	if t == 9:
		if g.key_b:
			open_door(tx, ty, "BLUE lock released")
		else:
			hud.show_msg("you need the BLUE keycard", "94")
		return
	if t == 7:
		g.tiles[ty * g.mw + tx] = 0
		rebuild_mapkind()
		g.secrets += 1
		fx_secret_glitter(tx + 0.5, ty + 0.5)
		hud.show_msg("you found a secret!", "96")
		return
	if t == 6:
		g.mode = 2      # tally
		g.mode_t = 0
		hud.show_msg("level complete", "92")
		return
	hud.show_msg("solid rock. very solid.", "90")


def open_door(tx, ty, msg):
	g = state.G
	g.tiles[ty * g.mw + tx] = 0
	rebuild_mapkind()
	fx_door_puff(tx + 0.5, ty + 0.5)
	g.noise = max(g.noise, 10)
	hud.show_msg(msg, "97")


# ------------------------------------------------------------ pickups -------
def give_ammo(t, n):
	g = state.G
	if g.ammo[t] >= AMMO_MAX[t]:
		return False
	g.ammo[t] = min(AMMO_MAX[t], g.ammo[t] + n)
	return True


def check_pickups():
	g = state.G
	for it in g.items:
		if it[3] != 1:
			kind = it[0]
			if dist2(g.px, g.py, it[1], it[2]) <= 0.36:
				got = False
				if kind == 0:
					if g.hp < 100:
						g.hp = min(100, g.hp + 10)
						got = True
				elif kind == 1:
					if g.hp < 100:
						g.hp = min(100, g.hp + 25)
						got = True
				elif kind == 2:
					if g.armor < 100:
						g.armor = min(100, g.armor + 50)
						got = True
				elif kind == 3:
					got = give_ammo(0, 20)
				elif kind == 4:
					got = give_ammo(1, 8)
				elif kind == 5:
					got = give_ammo(2, 30)
				elif kind == 6:
					g.key_r = True
					got = True
				elif kind == 7:
					g.key_b = True
					got = True
				elif kind == 8:
					g.have[1] = 1
					give_ammo(1, 8)
					g.weapon = 1
					g.cooldown = 8
					got = True
					g.face_grin = 30
				elif kind == 9:
					g.have[2] = 1
					give_ammo(2, 40)
					g.weapon = 2
					g.cooldown = 8
					got = True
					g.face_grin = 30
				elif kind == 10:
					g.hp = min(150, g.hp + 40)
					got = True
					g.face_grin = 20
				if got:
					it[3] = 1
					hud.show_msg("picked up " + ITEM_DEFS[kind]["name"], "93")
