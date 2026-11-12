"""The showcase look's feel (feel.jai): what the game does on screen when
something happens. Feel watches the game from outside, frame by frame,
compares what it sees with what it saw (a shot spent, an enemy flinching, ammo
gone, a door open) and answers with light sources, screen shake, view bob,
floor decals, hit markers, screen tints and the iris and title card that open a
floor. It reads the simulation and never writes to it. The reference's sound
cues are left out: the port is muted, and they never reach a frame."""

from math import cos, sin, sqrt

import render
import state
from canvas import FONT5, lettering_width
from colors import color_idx, mix_rgb, rgb
from lights import BAYER4, LIGHT_FIRE, LIGHT_HEX, LIGHT_ACID
from look import MOODS, TONE_ASH, TONE_BLOOD, TONE_SCORCH
from tune import BLAST_RADIUS, BOB_PIXELS, MUZZLE_RADIUS, SHAKE, SHOT_RADIUS, VIGNETTE
from util import dist2, iclamp, idivt, mix32, PAL_RGB, rgb_idx

_TOWARD = ((0.0, 0.0, 0.0), (0.0, 0.0, 0.0), (255.0, 222.0, 120.0), (120.0, 255.0, 230.0), (255.0, 236.0, 200.0))
_SHOT_LIGHT = (0, LIGHT_ACID, LIGHT_FIRE, LIGHT_HEX)


def fclamp(v, lo, hi):
	return lo if v < lo else (hi if v > hi else v)


class Feel:
	def __init__(self):
		self.yaw = 0.0              # shake: a turn of the view, radians
		self.pitch = 0              # bob and shake: pixel rows the horizon moves
		self.sources = []           # this frame's lights over the bake: (x, y, channel, radius, power)
		self.last_tick = 0
		self.last_map_ticks = 0
		self.last_hp = 100
		self.last_armor = 0
		self.last_ammo = [48, 0, 0]
		self.last_key_r = False
		self.last_key_b = False
		self.last_secrets = 0
		self.last_bob = 0.0
		self.bob_amp = 0.0
		self.e_flash = []
		self.e_alive = []
		self.shot_kind = []
		self.shot_x = []
		self.shot_y = []
		self.blasts = []            # a blast's afterglow: [x, y, channel, age]
		self.trauma = 0.0
		self.hurt_t = 0
		self.pickup_t = 0
		self.secret_t = 0
		self.blast_t = 0
		self.hit_t = 0
		self.hit_kill = False
		self.mask_w = 0
		self.mask_h = 0
		self.mask_px = []           # vignette: the pixels it darkens, and how much (1..4)
		self.mask_level = []
		self.mask_all = []          # every pixel's level, for the full-screen tints
		self.remaps = []            # [tint * 5 + level]: palette index -> tinted index, -1 not yet made
		self.card_shades = []

	def frame(self):
		"""once per frame, before the view draws"""
		g = state.G
		ticks = g.tick - self.last_tick
		self.last_tick = g.tick
		if ticks < 0:
			ticks = 0
		if g.mode != 1:
			return
		if g.map_ticks < self.last_map_ticks or len(g.enemies) != len(self.e_alive):
			self.new_floor()
		self.last_map_ticks = g.map_ticks
		for _ in range(ticks):
			self.age_one_tick()
		self.watch_player(ticks)
		self.watch_enemies()
		self.watch_shots()
		self.watch_world()
		self.stain_landed()
		render.dry_stains(g.tick)
		self.gather_lights()
		self.camera()

	def overlay(self, vw, vh):
		"""after the view, the gun and the particles: tints, the vignette, the hit marker, the floor's opening"""
		if self.mask_w != vw or self.mask_h != vh:
			self.build_mask(vw, vh)
		base = 0
		tint = -1
		if self.blast_t > 0:
			tint = 4
			base = min(3, self.blast_t)
		elif self.hurt_t > 0:
			tint = 1
			base = min(3, self.hurt_t // 2 + 1)
		elif self.pickup_t > 0:
			tint = 2
			base = min(2, self.pickup_t // 2)
		elif self.secret_t > 0:
			tint = 3
			base = min(2, self.secret_t // 3)
		elif state.G.hp <= 25 and (state.G.tick % 24) < 5:
			tint = 1
			base = 1
		self.shade_pass(0, 0)
		if tint >= 0:
			self.shade_pass(tint, base)
		self.draw_hit_marker(vw)
		self.opening(vw, vh)

	# --------------------------------------------------------------- watching
	def kick(self, amount):
		self.trauma = fclamp(self.trauma + amount, 0.0, 1.0)

	def new_floor(self):
		g = state.G
		self.e_flash = [e.flash for e in g.enemies]
		self.e_alive = [e.alive for e in g.enemies]
		self.shot_kind = []
		self.shot_x = []
		self.shot_y = []
		self.blasts = []
		self.last_secrets = g.secrets
		self.last_hp = g.hp
		self.last_armor = g.armor
		self.last_ammo = list(g.ammo)
		self.card_shades = []
		self.trauma = 0.0
		self.hurt_t = 0
		self.pickup_t = 0
		self.secret_t = 0
		self.blast_t = 0
		self.hit_t = 0

	def age_one_tick(self):
		trauma = self.trauma * 0.86
		self.trauma = 0.0 if trauma < 0.01 else trauma
		if self.hurt_t > 0:
			self.hurt_t -= 1
		if self.pickup_t > 0:
			self.pickup_t -= 1
		if self.secret_t > 0:
			self.secret_t -= 1
		if self.blast_t > 0:
			self.blast_t -= 1
		if self.hit_t > 0:
			self.hit_t -= 1
		kept = []
		for b in self.blasts:
			b[3] += 1
			if b[3] < 14:
				kept.append(b)
		self.blasts = kept

	def watch_player(self, ticks):
		"""everything that fades does so per tick, so the feel is the same at any frame rate"""
		g = state.G
		lost = (self.last_hp - g.hp) + (self.last_armor - g.armor)
		if g.hp < self.last_hp:
			self.hurt_t = 8
			self.kick(0.25 + lost / 30.0)
		ammo = g.ammo
		last_ammo = self.last_ammo
		for pool in range(3):
			if ammo[pool] < last_ammo[pool]:
				weapon = g.weapon
				if weapon == 0:
					self.kick(0.10)
				elif weapon == 1:
					self.kick(0.42)
				else:
					self.kick(0.18)
			elif ammo[pool] > last_ammo[pool]:
				self.pickup_t = 5
		if (g.key_r and not self.last_key_r) or (g.key_b and not self.last_key_b):
			self.pickup_t = 6
		if g.hp > self.last_hp or g.armor > self.last_armor:
			self.pickup_t = 5
		self.last_hp = g.hp
		self.last_armor = g.armor
		self.last_ammo = list(ammo)
		self.last_key_r = g.key_r
		self.last_key_b = g.key_b
		moved = g.bob - self.last_bob
		self.last_bob = g.bob
		for _ in range(ticks):
			self.bob_amp = fclamp(self.bob_amp + 0.25, 0.0, 1.0) if moved > 0.0 else self.bob_amp * 0.8

	def watch_enemies(self):
		g = state.G
		e_flash = self.e_flash
		e_alive = self.e_alive
		for i, e in enumerate(g.enemies):
			if e.flash > e_flash[i] and e_alive[i]:
				self.hit_t = 6
				self.hit_kill = False
			if e_alive[i] and not e.alive:
				self.hit_t = 9
				self.hit_kill = True
				self.kick(0.6 if e.kind == 3 else 0.12)
				render.stain_floor(e.x, e.y, 0.0 if e.kind == 2 else 0.34, TONE_BLOOD, 170)
			e_flash[i] = e.flash
			e_alive[i] = e.alive

	def watch_shots(self):
		"""a shot gone since the last frame burst somewhere near where it was"""
		g = state.G
		shots = g.shots
		matched = [False] * len(shots)
		shot_kind = self.shot_kind
		shot_x = self.shot_x
		shot_y = self.shot_y
		for i in range(len(shot_kind)):
			found = False
			for j, s in enumerate(shots):
				if not matched[j] and s[0] == shot_kind[i] and dist2(s[1], s[2], shot_x[i], shot_y[i]) < 1.2:
					matched[j] = True
					found = True
					break
			if found:
				continue
			kind = shot_kind[i]
			if kind == 1:
				continue
			self.blasts.append([shot_x[i], shot_y[i], LIGHT_HEX if kind == 3 else LIGHT_FIRE, 0])
			render.stain_floor(shot_x[i], shot_y[i], 0.55, TONE_SCORCH, 190)
			render.stain_floor(shot_x[i], shot_y[i], 0.8, TONE_ASH, 40)
			d = sqrt(dist2(shot_x[i], shot_y[i], g.px, g.py))
			if d < 6.0:
				self.kick(0.75 * (1.0 - d / 6.0) + 0.1)
				self.blast_t = max(self.blast_t, 3 if d < 3.0 else 2)
		self.shot_kind = [s[0] for s in shots]
		self.shot_x = [s[1] for s in shots]
		self.shot_y = [s[2] for s in shots]

	def watch_world(self):
		g = state.G
		if g.secrets > self.last_secrets:
			self.secret_t = 9
		self.last_secrets = g.secrets

	def stain_landed(self):
		"""blood and meat that reached the floor leave their mark"""
		stain_floor = render.stain_floor
		for p in state.G.fx.pool:
			kind = p[0]
			if (kind == 2 or kind == 3) and p[3] <= 0.021:
				stain_floor(p[1], p[2], 0.09 if kind == 3 else 0.06, TONE_BLOOD, 230)

	# ----------------------------------------------------------------- light
	def gather_lights(self):
		g = state.G
		sources = []
		if g.muzzle > 0:
			dx = cos(g.pang)
			dy = sin(g.pang)
			channel = LIGHT_HEX if g.weapon == 2 else LIGHT_FIRE
			sources.append((g.px + dx * 0.6, g.py + dy * 0.6, channel, MUZZLE_RADIUS * (0.55 + 0.15 * g.muzzle), 1.0))
		for s in g.shots:
			sources.append((s[1], s[2], _SHOT_LIGHT[s[0]], SHOT_RADIUS, 0.9))
		for b in self.blasts:
			t = 1.0 - b[3] / 14.0
			sources.append((b[0], b[1], b[2], BLAST_RADIUS * (0.4 + 0.6 * t), 0.35 + 0.65 * t))
		self.sources = sources

	# ---------------------------------------------------------------- camera
	def camera(self):
		"""shake is trauma squared, so small knocks barely move the view and big ones rattle it"""
		g = state.G
		pixh = render.PIXH
		shake = self.trauma * self.trauma * SHAKE
		n1 = (mix32(g.tick, 101) % 2001) / 1000.0 - 1.0
		n2 = (mix32(g.tick, 202) % 2001) / 1000.0 - 1.0
		self.yaw = shake * 0.045 * n1
		bob = (abs(cos(g.bob * 0.5)) - 0.5) * 2.0 * BOB_PIXELS * self.bob_amp * pixh / 2.0
		self.pitch = int(bob + shake * 5.0 * pixh / 2.0 * n2)

	# ------------------------------------------------------------- overlays
	def build_mask(self, vw, vh):
		self.mask_w = vw
		self.mask_h = vh
		mask_px = []
		mask_level = []
		mask_all = []
		cx = vw / 2.0
		cy = vh / 2.0
		for y in range(vh):
			for x in range(vw):
				dx = (x + 0.5 - cx) / cx
				dy = (y + 0.5 - cy) / cy
				e = dx * dx * 0.8 + dy * dy
				level = iclamp(int((e - 0.5) * 3.2 + BAYER4[(y & 3) * 4 + (x & 3)]), 0, 4)
				mask_all.append(level)
				if level > 0:
					mask_px.append(y * vw + x)
					mask_level.append(level)
		self.mask_px = mask_px
		self.mask_level = mask_level
		self.mask_all = mask_all

	def shade_pass(self, tint, base):
		"""tint 0 darkens the corners; the others wash the screen from the edges in:
		1 hurt, 2 pickup, 3 secret, 4 blast"""
		remaps = self.remaps
		while len(remaps) < 25:
			remaps.append([])
		pal = len(PAL_RGB)
		for level in range(1, 5):
			table = remaps[tint * 5 + level]
			if len(table) < pal:
				table.extend([-1] * (pal - len(table)))
		pix = render.PIX
		remapped = self.remapped
		if tint == 0:
			if VIGNETTE <= 0.0:
				return
			mask_level = self.mask_level
			for k, i in enumerate(self.mask_px):
				pix[i] = remapped(0, mask_level[k], pix[i])
			return
		mask_all = self.mask_all
		for i in range(len(pix)):
			level = mask_all[i] + base
			if level > 4:
				level = 4
			if level > 0:
				pix[i] = remapped(tint, level, pix[i])

	def remapped(self, tint, level, p):
		table = self.remaps[tint * 5 + level]
		m = table[p]
		if m >= 0:
			return m
		key = PAL_RGB[p]
		r = float((key >> 16) & 255)
		g = float((key >> 8) & 255)
		b = float(key & 255)
		a = level / 4.0
		if tint == 0:
			f = 1.0 - VIGNETTE * a
			m = rgb_idx(int(r * f), int(g * f), int(b * f))
		elif tint == 1:
			m = rgb_idx(int(r + (255.0 - r) * a * 0.55), int(g * (1.0 - a * 0.65)), int(b * (1.0 - a * 0.65)))
		else:
			toward = _TOWARD[tint]
			k = a * 0.55 if tint == 4 else a * 0.32
			m = rgb_idx(int(r + (toward[0] - r) * k), int(g + (toward[1] - g) * k), int(b + (toward[2] - b) * k))
		table[p] = m
		return m

	def draw_hit_marker(self, vw):
		"""four ticks around the aim point: white on a hit, red and wider on a kill"""
		g = state.G
		if self.hit_t <= 0 or g.automap:
			return
		color = rgb_idx(255, 64, 48) if self.hit_kill else rgb_idx(255, 250, 236)
		pixw = render.PIXW
		pixh = render.PIXH
		cx = vw // 2
		cy = render.HORIZON
		gap = (3 if self.hit_kill else 2) * pixw
		pix = render.PIX
		vh = g.vh
		for k in range(2 * pixw):
			ox = gap + k
			oy = (gap + k) * pixh // pixw // 2 + 1
			for s in range(4):
				x = cx + (ox if (s & 1) == 0 else -ox)
				y = cy + (oy if (s & 2) == 0 else -oy)
				if 0 <= x < vw and 0 <= y < vh:
					pix[y * vw + x] = color

	def opening(self, vw, vh):
		"""a floor opens with an iris from the center, then its name, which fades into the dark"""
		g = state.G
		t = g.map_ticks
		if t >= 80:
			return
		pix = render.PIX
		pixw = render.PIXW
		pixh = render.PIXH
		if t < 16:
			# in cell widths: a pixel is 1 / PIXW of a cell across and 2 / PIXH down
			cx = vw / 2.0
			cy = vh / 2.0
			sx = 1.0 / pixw
			sy = 2.0 / pixh
			r = (t / 16.0) * sqrt(cx * sx * cx * sx + cy * sy * cy * sy) * 1.02
			r2 = r * r
			for y in range(vh):
				dy = (y + 0.5 - cy) * sy
				row = y * vw
				for x in range(vw):
					dx = (x + 0.5 - cx) * sx
					if dx * dx + dy * dy > r2:
						pix[row + x] = 0
		if t < 6 or g.automap:
			return
		if len(self.card_shades) == 0:
			self.build_card_shades()
		fade = 0 if t < 60 else (t - 60) // 4
		if fade >= 5:
			return
		name = g.map_name
		code = name[:4]
		title = name[6:]
		# a font pixel spans sx view pixels across and sy down, so letters stay square on 1:2 cells
		sx = max(1, pixw * 2 // pixh)
		scale = 3
		while scale > 1 and lettering_width(title, scale) * sx > vw - 8:
			scale -= 1
		top = vh // 5
		shades = self.card_shades
		self.pix_lettering(code, idivt(vw - lettering_width(code, 1) * sx, 2), top, 1, sx, shades[fade + 5], shades[10])
		self.pix_lettering(title, idivt(vw - lettering_width(title, scale) * sx, 2), top + 8, scale, sx, shades[fade],
			shades[10])

	def build_card_shades(self):
		mood = MOODS[state.G.map_i]
		bone = rgb(250, 240, 220)
		shades = [color_idx(mix_rgb(bone, mood.fog, i / 5.0)) for i in range(5)]
		shades.extend(color_idx(mix_rgb(mood.light, mood.fog, i / 5.0)) for i in range(5))
		shades.append(rgb_idx(4, 2, 4))
		self.card_shades = shades

	def pix_lettering(self, s, x, y, scale, sx, color, shadow):
		"""the 3x5 font straight into the view's pixels, a font pixel scale rows tall and scale * sx columns wide"""
		pix = render.PIX
		g = state.G
		vw = g.vw
		vh = g.vh
		sy = scale
		cw = sx * scale
		for pas in range(2):
			ink = shadow if pas == 0 else color
			off = 1 if pas == 0 else 0
			for i, ch in enumerate(s):
				glyph_rows = FONT5.get(ch)
				if glyph_rows is None:
					continue
				gx = x + i * 4 * cw
				for ry in range(5):
					for rx in range(3):
						if glyph_rows[ry][rx] != "#":
							continue
						for yy in range(sy):
							py = y + (ry + off) * sy + yy
							if py < 0 or py >= vh:
								continue
							rb = py * vw
							for xx in range(cw):
								qx = gx + (rx + off) * cw + xx
								if 0 <= qx < vw:
									pix[rb + qx] = ink
