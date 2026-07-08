"""Game state + the fixed-timestep tick + the deterministic autopilot.
G is the single world object (created once in gloom.boot); the autopilot is a
generator yielding one Input per tick, living in a field like the enemy
brains."""

from math import atan2, floor, sqrt
from typing import NamedTuple

import combat
import hud
import maps
import render
import sim
import state
from data import AMMO_MAX, MAPS, WEAPONS
from particles import ParticlePool, PART_MAX
from util import ang_diff, dist2, mix32, trunc
from world import tile_at


class Input(NamedTuple):
	fwd: bool = False
	back: bool = False
	sl: bool = False
	sr: bool = False
	tl: bool = False
	tr: bool = False
	run: bool = False
	fire: bool = False
	use: bool = False
	weapon: int = -1
	start: bool = False


def pilot_input(fwd, back, sl, sr, turn, fire, use, wsel, start):
	return Input(fwd=fwd, back=back, sl=sl, sr=sr, tl=turn < 0, tr=turn > 0,
		run=True, fire=fire, use=use, weapon=wsel, start=start)


class Game:
	def __init__(self, w, h):
		# console + view geometry (sub-cell pixels: vw = w*PIXW, vh = rows*PIXH)
		self.w = w
		self.h = h
		self.vw = w * render.PIXW
		self.vh = (h - 6) * render.PIXH

		# flow
		self.mode = 0          # 0 title, 1 play, 2 tally, 3 dead, 4 victory
		self.mode_t = 0
		self.tick = 0
		self.accum = 0.0
		self.quit = False
		self.autopilot = False
		self.frame_key = ""
		self.hash = 2166136261

		# map
		self.map_i = 0
		self.map_name = ""
		self.par = 0
		self.mw = 0
		self.mh = 0
		self.tiles = []
		self.mapkind = None    # per-cell render kinds (raycast snapshot)

		# player
		self.px = 2.5
		self.py = 2.5
		self.pang = 0.0
		self.hp = 100
		self.armor = 0
		self.ammo = [48, 0, 0]
		self.have = [1, 0, 0]
		self.weapon = 0
		self.key_r = False
		self.key_b = False
		self.cooldown = 0
		self.bob = 0.0
		self.muzzle = 0
		self.gun_kick = 0
		self.death_cause = "the gloom"

		# world
		self.enemies = []
		self.items = []        # [kind, x, y, taken]
		self.shots = []        # [kind, x, y, vx, vy, ttl]
		self.fx = ParticlePool()
		self.noise = 0
		self.warden_down = False

		# feedback
		self.msg = ""
		self.msg_color = "97"
		self.msg_t = 0
		self.face_pain = 0
		self.face_grin = 0
		self.hurt_flash = 0
		self.automap = False

		# tallies
		self.kills = 0
		self.kill_total = 0
		self.secrets = 0
		self.secret_total = 0
		self.map_ticks = 0
		self.ep_kills = 0
		self.ep_kill_total = 0
		self.ep_secrets = 0
		self.ep_secret_total = 0
		self.ep_ticks = 0
		self.deaths = 0

		# render scratch (allocated once; bg rebaked per map)
		self.pix = [0] * (self.vw * self.vh)
		self.bg = []
		self.zbuf = [99.0] * self.vw
		self.lutc = {}

		# the autopilot: a generator handle living in a field
		self.pilot = None
		self.input = Input()

	def reset_player(self):
		self.hp = 100
		self.armor = 0
		self.ammo = [48, 0, 0]
		self.have = [1, 0, 0]
		self.weapon = 0
		self.cooldown = 0
		self.bob = 0.0

	def start_episode(self):
		self.map_i = 0
		self.ep_kills = 0
		self.ep_kill_total = 0
		self.ep_secrets = 0
		self.ep_secret_total = 0
		self.ep_ticks = 0
		self.deaths = 0
		self.reset_player()
		self.begin_map()

	def begin_map(self):
		self.key_r = False
		self.key_b = False
		self.shots = []
		self.fx.reset()
		maps.load_map(self.map_i)
		self.mode = 1
		self.mode_t = 0
		hud.show_msg(self.map_name, "97")

	def finish_map(self):
		self.ep_kills += self.kills
		self.ep_kill_total += self.kill_total
		self.ep_secrets += self.secrets
		self.ep_secret_total += self.secret_total
		self.ep_ticks += self.map_ticks
		self.map_i += 1
		if self.map_i >= len(MAPS):
			self.mode = 4
			self.mode_t = 0
		else:
			self.begin_map()

	def gather_input(self):
		"""deterministic input for this tick, human or generator"""
		if self.autopilot:
			pilot = self.pilot
			if pilot is None or pilot.gi_frame is None:
				pilot = self.pilot_brain()
				self.pilot = pilot
			return next(pilot)
		key_down = state.key_down
		fk = self.frame_key
		fire_edge = fk == " " or fk == "enter"
		wsel = -1
		if fk == "1":
			wsel = 0
		elif fk == "2":
			wsel = 1
		elif fk == "3":
			wsel = 2
		return Input(
			fwd=key_down("w") or key_down("up"),
			back=key_down("s") or key_down("down"),
			sl=key_down("a"),
			sr=key_down("d"),
			tl=key_down("left"),
			tr=key_down("right"),
			run=key_down("shift"),
			fire=key_down("space") or key_down("ctrl"),
			use=fk == "e",
			weapon=wsel,
			start=fire_edge or key_down("space") or key_down("ctrl"),
		)

	def run_tick(self):
		self.tick += 1
		inp = self.gather_input()
		self.input = inp
		start_pressed = inp.start
		mode = self.mode
		if mode == 0:
			self.mode_t += 1
			if start_pressed and self.mode_t > 8:
				self.start_episode()
			self.accumulate_hash()
			return
		if mode == 2:
			self.mode_t += 1
			if start_pressed and self.mode_t > 25:
				self.finish_map()
			self.accumulate_hash()
			return
		if mode == 3:
			self.mode_t += 1
			if start_pressed and self.mode_t > 25:
				self.deaths += 1
				self.reset_player()
				self.begin_map()       # the floor repopulates; the Well remembers nothing
			self.accumulate_hash()
			return
		if mode == 4:
			self.mode_t += 1
			self.accumulate_hash()
			return

		# --- play ---
		wsel = inp.weapon
		if wsel >= 0 and self.have[wsel] == 1 and wsel != self.weapon:
			self.weapon = wsel
			self.cooldown = 8
			hud.show_msg(WEAPONS[wsel]["name"] + " ready", "97")
		if self.frame_key == "m":
			self.automap = not self.automap
		if inp.use:
			sim.player_use()
		sim.player_move(inp)
		if inp.fire:
			combat.player_fire()

		if self.cooldown > 0:
			self.cooldown -= 1
		if self.muzzle > 0:
			self.muzzle -= 1
		if self.gun_kick > 0:
			self.gun_kick -= 1
		if self.noise > 0:
			self.noise -= 1
		if self.msg_t > 0:
			self.msg_t -= 1
		if self.face_pain > 0:
			self.face_pain -= 1
		if self.face_grin > 0:
			self.face_grin -= 1
		if self.hurt_flash > 0:
			self.hurt_flash -= 1

		# live length each iteration: warden summons APPEND mid-loop and tick
		# the same tick (the reference range-for re-checks size per iteration)
		enemies = self.enemies
		i = 0
		while i < len(enemies):
			enemies[i].tick()
			i += 1
		combat.update_shots()
		self.fx.update()
		self.map_ticks += 1
		self.accumulate_hash()

	def accumulate_hash(self):
		mix = mix32
		hh = self.hash
		hh = mix(hh, self.tick)
		hh = mix(hh, self.mode * 31 + self.map_i)
		hh = mix(hh, trunc(self.px * 256.0) * 4096 + trunc(self.py * 256.0))
		hh = mix(hh, trunc(self.pang * 1024.0))
		hh = mix(hh, self.hp * 512 + self.armor)
		ammo = self.ammo
		hh = mix(hh, ammo[0] * 65536 + ammo[1] * 256 + ammo[2])
		hh = mix(hh, self.weapon * 64 + (2 if self.key_r else 0) + (1 if self.key_b else 0))
		live = 0
		acc = 0
		for e in self.enemies:
			if e.alive:
				live += 1
				acc = (acc + trunc(e.x * 64.0) * 977 + trunc(e.y * 64.0) * 331 + e.hp * 7) & 0xFFFFFFFF
		hh = mix(hh, live)
		hh = mix(hh, acc)
		pacc = 0
		for p in self.fx.pool:
			pk = p[0]
			if pk != 0:
				pacc = (pacc + pk * 131 + (trunc(p[1] * 16.0) * 61 + trunc(p[2] * 16.0)) * 17 + p[7]) & 0xFFFFFFFF
		hh = mix(hh, pacc)
		hh = mix(hh, len(self.shots) * 8191 + self.kills * 127 + self.secrets * 31)
		hh = mix(hh, state.RNG.state())
		self.hash = hh

	# ------------------------------------------------------- autopilot ------
	def pilot_brain(self):
		"""One generator, one yield per tick, every decision from game state + RNG."""
		dist_field = []
		field_age = 999
		goal_x = -1
		goal_y = -1
		strafe_dir = 1
		strafe_t = 0
		while True:
			if self.mode != 1:
				yield pilot_input(False, False, False, False, 0, False, False, -1, True)
				continue

			# --- combat: nearest visible enemy inside 12 tiles. Below 40 hp the
			# pilot disengages unless cornered — navigation will chase medkits.
			desperate = self.hp < 40
			found = False
			tgt_x = 0.0
			tgt_y = 0.0
			best_d = 12.0
			px, py = self.px, self.py
			for e in self.enemies:
				if e.alive:
					d = sqrt(dist2(e.x, e.y, px, py))
					if d < best_d and combat.los_clear(px, py, e.x, e.y):
						best_d = d
						tgt_x = e.x
						tgt_y = e.y
						found = True
			if found and desperate and best_d > 3.0:
				found = False
			if found:
				want = atan2(tgt_y - py, tgt_x - px)
				da = ang_diff(self.pang, want)
				turn = 0
				if da > 0.05:
					turn = 1
				if da < -0.05:
					turn = -1
				# weapon sense: scatter close, hex for crowds/far, pistol default
				wsel = -1
				have, ammo = self.have, self.ammo
				if have[1] == 1 and ammo[1] > 0 and best_d < 6.0:
					wsel = 1
				elif have[2] == 1 and ammo[2] > 0 and best_d > 3.5:
					wsel = 2
				elif ammo[0] > 0:
					wsel = 0
				elif have[1] == 1 and ammo[1] > 0:
					wsel = 1
				elif have[2] == 1 and ammo[2] > 0:
					wsel = 2
				if wsel == self.weapon:
					wsel = -1
				aligned = -0.14 < da < 0.14
				fire = aligned and self.cooldown == 0 and best_d > 1.2
				# footwork: back off rushers, strafe under fire
				back = best_d < 2.4
				fwd = best_d > 8.5 and aligned
				strafe_t += 1
				if strafe_t > 18:
					strafe_t = 0
					if state.RNG.chance(60):
						strafe_dir = -strafe_dir
				yield pilot_input(fwd, back, strafe_dir < 0, strafe_dir > 0, turn, fire, False, wsel, False)
				continue

			# --- navigate: pick a goal, BFS-from-goal field, walk downhill.
			# COMMIT to the goal until arrival; the field alone refreshes on a
			# timer so newly opened doors shorten the route.
			field_age += 1
			goal_gone = goal_x < 0 or field_age > 240
			if not goal_gone:
				if tile_at(goal_x, goal_y) == 0 and floor(px) == goal_x and floor(py) == goal_y:
					goal_gone = True
			if goal_gone:
				goal_x, goal_y = self.pick_goal()
				dist_field = self.bfs_field(goal_x, goal_y)
				field_age = 0
			elif field_age % 45 == 0:
				dist_field = self.bfs_field(goal_x, goal_y)
			if goal_x < 0:
				# nothing to want: spin slowly (a lost pilot is still deterministic)
				yield pilot_input(False, False, False, False, 1, False, False, -1, False)
				continue
			mw, mh = self.mw, self.mh
			ptx = floor(px)
			pty = floor(py)
			here = dist_field[pty * mw + ptx]
			if here < 0:
				# goal unreachable from here (shouldn't happen): drop it
				goal_x = -1
				yield pilot_input(False, False, False, False, 0, False, False, -1, False)
				continue
			# pick the walkable-or-door neighbor closest to the goal
			nx = ptx
			ny = pty
			bestv = here
			cx = ptx + 1
			cy = pty
			if cx < mw:
				v = dist_field[cy * mw + cx]
				if 0 <= v < bestv:
					bestv = v
					nx, ny = cx, cy
			cx = ptx - 1
			if cx >= 0:
				v = dist_field[cy * mw + cx]
				if 0 <= v < bestv:
					bestv = v
					nx, ny = cx, cy
			cx = ptx
			cy = pty + 1
			if cy < mh:
				v = dist_field[cy * mw + cx]
				if 0 <= v < bestv:
					bestv = v
					nx, ny = cx, cy
			cy = pty - 1
			if cy >= 0:
				v = dist_field[cy * mw + cx]
				if 0 <= v < bestv:
					bestv = v
					nx, ny = cx, cy
			wx = nx + 0.5
			wy = ny + 0.5
			want2 = atan2(wy - py, wx - px)
			da2 = ang_diff(self.pang, want2)
			turn2 = 0
			if da2 > 0.08:
				turn2 = 1
			if da2 < -0.08:
				turn2 = -1
			fwd2 = -0.6 < da2 < 0.6
			# a door/secret in the way? use it when close and mostly facing
			front_t = tile_at(nx, ny)
			use2 = front_t != 0 and dist2(px, py, wx, wy) < 2.1 and fwd2
			yield pilot_input(fwd2, False, False, False, turn2, False, use2, -1, False)

	# goal priority: medkits when hurting -> live enemies -> loot -> unfound
	# secrets -> the exit
	MED_KINDS = (0, 1, 10)
	ALL_KINDS = (0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10)

	def pick_goal(self):
		if self.hp < 55:
			mx, my = self.nearest_item(self.MED_KINDS)
			if mx >= 0:
				return mx, my
		hunt_x = -1
		hunt_y = -1
		hd = 9999.0
		px, py = self.px, self.py
		for e in self.enemies:
			if e.alive:
				d = dist2(e.x, e.y, px, py)
				if d < hd:
					hd = d
					hunt_x = floor(e.x)
					hunt_y = floor(e.y)
		if hunt_x >= 0:
			return hunt_x, hunt_y
		lx, ly = self.nearest_item(self.ALL_KINDS)
		if lx >= 0:
			return lx, ly
		if self.secrets < self.secret_total:
			sx, sy = self.nearest_secret()
			if sx >= 0:
				return sx, sy
		return self.exit_tile()

	def nearest_item(self, kinds):
		bx = -1
		by = -1
		bd = 9999.0
		px, py = self.px, self.py
		hp, armor = self.hp, self.armor
		ammo = self.ammo
		for it in self.items:
			if it[3] != 1:
				k = it[0]
				wanted = k in kinds
				# skip anything the pickup code would refuse
				if wanted:
					if (k == 0 or k == 1) and hp >= 100:
						wanted = False
					if k == 10 and hp >= 150:
						wanted = False
					if k == 2 and armor >= 100:
						wanted = False
					if k == 3 and ammo[0] >= AMMO_MAX[0]:
						wanted = False
					if k == 4 and ammo[1] >= AMMO_MAX[1]:
						wanted = False
					if k == 5 and ammo[2] >= AMMO_MAX[2]:
						wanted = False
					# keys we already carry are not loot
					if k == 6 and self.key_r:
						wanted = False
					if k == 7 and self.key_b:
						wanted = False
				if wanted:
					d = dist2(it[1], it[2], px, py)
					if d < bd:
						bd = d
						bx = floor(it[1])
						by = floor(it[2])
		return bx, by

	def nearest_secret(self):
		bx = -1
		by = -1
		bd = 9999.0
		mw = self.mw
		tiles = self.tiles
		px, py = self.px, self.py
		for ty in range(self.mh):
			for tx in range(mw):
				if tiles[ty * mw + tx] == 7:
					d = dist2(tx + 0.5, ty + 0.5, px, py)
					if d < bd:
						bd = d
						bx = tx
						by = ty
		return bx, by

	def exit_tile(self):
		mw = self.mw
		tiles = self.tiles
		for ty in range(self.mh):
			for tx in range(mw):
				if tiles[ty * mw + tx] == 6:
					return tx, ty
		return -1, -1

	def bfs_field(self, gx, gy):
		"""BFS distances FROM the goal, over floor + doors this pilot can open
		(+ the goal tile itself so walls like secrets/exit are approachable)"""
		mw, mh = self.mw, self.mh
		field = [-1] * (mw * mh)
		if gx < 0:
			return field
		tiles = self.tiles
		key_r, key_b = self.key_r, self.key_b
		queue = [(gx, gy)]
		field[gy * mw + gx] = 0
		head = 0
		while head < len(queue):
			cx, cy = queue[head]
			head += 1
			cd = field[cy * mw + cx]
			for nx, ny in ((cx + 1, cy), (cx - 1, cy), (cx, cy + 1), (cx, cy - 1)):
				if 0 <= nx < mw and 0 <= ny < mh:
					fi = ny * mw + nx
					if field[fi] < 0:
						t = tiles[fi]
						ok = t == 0 or t == 5 or (t == 8 and key_r) or (t == 9 and key_b)
						if ok:
							field[fi] = cd + 1
							queue.append((nx, ny))
		return field
