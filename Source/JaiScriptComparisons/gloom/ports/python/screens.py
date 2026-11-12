"""The showcase look's status bar, panel and full-screen pages (title, floor
tally, death, victory), painted on half-block canvases (screens.jai). hud.py
keeps the reference look's versions, the frames --smoke hashes by default."""

import hud
import render
import state
from canvas import Canvas, lettering_width
from colors import color_idx, mix_rgb, rgb
from data import AMMO_MAX, MAPS, WEAPONS
from defs import parse_art
from look import MOODS
from util import iclamp, idivt, mix32, rgb_idx

# the face in the panel: 10x10 pixel art, one per expression (hud.face_name)
FACE_PAL = {"h": (58, 40, 30), "s": (214, 160, 120), "S": (156, 106, 78), "e": (240, 236, 226), "p": (34, 30, 40),
	"B": (70, 44, 30), "m": (96, 30, 30), "t": (236, 228, 210), "b": (186, 26, 22), "g": (150, 150, 140), "x": (40, 20, 20)}
FACE_SRC = {
	"ok":   ("..hhhhhh..", ".hhhhhhhh.", ".hsssssshh", ".sBBssBBsh", ".sepssepsS",
	         ".sssSsssss", ".ssSSSssS.", ".ssmmmmsS.", "..ssssss..", "...SSSS..."),
	"hurt": ("..hhhhhh..", ".hhhhhhhh.", ".hsssssshh", ".sBBssBBsh", ".sepssepsS",
	         ".sssSssSSs", ".ssSSSsSS.", ".ssmmmmsS.", "..sSssss..", "...SSSS..."),
	"bad":  ("..hhhhhh..", ".hhbhhhhh.", ".hsbsssshh", ".sBBssBBsh", ".sxxsseps.",
	         ".bssSssSSs", ".bsSSSsSS.", ".ssmmmmsS.", "..sSsSss..", "...SSSS..."),
	"crit": ("..hhbhhh..", ".hhbbhhhh.", ".hbbsbsshh", ".sBBbsBBsh", ".sxxssxxb.",
	         ".bbsSssSbs", ".bsSSSsbS.", ".sbttttbS.", "..sbsSsb..", "...SbSS..."),
	"pain": ("..hhhhhh..", ".hhhhhhhh.", ".hsBBsBBhh", ".sssssssh.", ".sxxssxxs.",
	         ".sssSsssss", ".ssmmmmsS.", ".ssmttmsS.", "..smmmms..", "...SSSS..."),
	"grin": ("..hhhhhh..", ".hhhhhhhh.", ".hsssssshh", ".sBBssBBsh", ".sepssepsS",
	         ".sssSsssss", ".smSSSSms.", ".sttttttS.", "..smmmms..", "...SSSS..."),
	"dead": ("..hhhhhh..", ".hhhbhhhh.", ".hgggggghh", ".gxgggxgg.", ".ggxggxgg.",
	         ".gxgggxgg.", ".gggSgggg.", ".ggxxxxgb.", "..gggggb..", "...gggb..."),
}

# the logo, 57 x 11 pixels: G L O O M
LOGO_ART = (
	"..######....###...........######......######....###....###",
	".########...###..........########....########...####..####",
	"###....###..###.........###....###..###....###..##.####.##",
	"##..........###.........##......##..##......##..##..##..##",
	"##..........###.........##......##..##......##..##......##",
	"##...#####..###.........##......##..##......##..##......##",
	"##...#####..###.........##......##..##......##..##......##",
	"##......##..###.........##......##..##......##..##......##",
	"###....###..###.........###....###..###....###..##......##",
	".########...#########....########....########...##......##",
	"..######....#########.....######......######....##......##",
)


def ember(i, salt):
	return mix32(mix32(i * 2654435 + 7, salt), 99991)


def blink(period):
	return (state.G.tick // period) % 2 == 0


def plain_lut(a):
	"""a code -> palette index table for an art, unshaded"""
	return [0] + [rgb_idx(c[0], c[1], c[2]) for c in a.rgb[1:]]


def shades5(top, bottom):
	"""five font-row colors from top to bottom"""
	return [color_idx(mix_rgb(top, bottom, i / 4.0)) for i in range(5)]


def message_color(code):
	"""the status-bar color of a message (show_msg takes an SGR color code)"""
	if code == "91":
		return rgb_idx(255, 96, 80)
	if code == "92":
		return rgb_idx(120, 236, 120)
	if code == "93":
		return rgb_idx(255, 214, 90)
	if code == "94":
		return rgb_idx(110, 160, 255)
	if code == "96":
		return rgb_idx(110, 230, 230)
	if code == "90":
		return rgb_idx(130, 126, 136)
	return rgb_idx(240, 236, 228)


def clock_text(ticks):
	secs = ticks // 30
	pad = "0" if secs % 60 < 10 else ""
	return f"{secs // 60}:{pad}{secs % 60}"


class Screens:
	def __init__(self, w, h):
		self.panel = Canvas(w, 6)      # the six rows under the view
		self.page = Canvas(w, h)       # a whole-screen page
		self.faces = {}
		self.face_luts = {}
		for name in sorted(FACE_SRC):
			art = parse_art({"pal": FACE_PAL, "rows": FACE_SRC[name]})
			self.faces[name] = art
			self.face_luts[name] = plain_lut(art)
		# the title's fire: one step a tick, the flicker a hash of the tick
		self.heat = []
		self.fire_colors = []
		self.fire_tick = -1

	def frame(self, fps, ms_sim, ms_draw):
		g = state.G
		g.feel.frame()
		mode = g.mode
		if mode == 0:
			return "\n".join(self.title_page())
		if mode == 2:
			return "\n".join(self.tally_page())
		if mode == 3:
			return "\n".join(self.dead_page())
		if mode == 4:
			return "\n".join(self.victory_page())
		rows = render.render_view()
		rows.extend(self.hud_rows(fps, ms_sim, ms_draw))
		return "\n".join(rows)

	# ---------------------------------------------------------------- HUD ----
	def hud_rows(self, fps, ms_sim, ms_draw):
		"""the status bar, then health, armor, the face, the weapon's ammo and the arms rack with keycards"""
		g = state.G
		c = self.panel
		w = c.w
		mood = MOODS[g.map_i]
		c.clear(rgb_idx(14, 12, 18))
		c.gradient(2, 12, rgb(62, 58, 70), rgb(22, 20, 26))
		c.rect(0, 11, w, 1, rgb_idx(10, 8, 12))

		# status bar: where, how it is going, what just happened
		dim = rgb_idx(132, 124, 136)
		map_code = g.map_name[:4]
		map_title = g.map_name[6:]
		c.text(1, 0, map_code, color_idx(mood.light))
		c.text(6, 0, map_title, rgb_idx(214, 206, 196))
		tally = f"KILLS {g.kills}/{g.kill_total}   SECRETS {g.secrets}/{g.secret_total}"
		tally_x = 7 + len(map_title) + 3
		c.text(tally_x, 0, tally, dim)
		if g.msg_t > 0:
			room = w - (tally_x + len(tally)) - 3
			m = g.msg[:max(0, room)] if len(g.msg) > room else g.msg
			c.text_right(w - 1, 0, m, message_color(g.msg_color))

		stat_w = iclamp(idivt(w - 33, 3), 12, 17)
		x_hp = 1
		x_armor = x_hp + stat_w + 1
		x_face = x_armor + stat_w + 1
		x_ammo = x_face + 13
		x_arms = x_ammo + stat_w + 1
		sep = rgb_idx(14, 12, 18)
		for x in (x_armor - 1, x_face - 1, x_ammo - 1, x_arms - 1):
			c.rect(x, 3, 1, 8, sep)

		red_digits = shades5(rgb(255, 220, 130), rgb(214, 64, 40))
		blue_digits = shades5(rgb(200, 232, 255), rgb(64, 120, 220))
		gold_digits = shades5(rgb(255, 244, 196), rgb(214, 150, 54))
		hp = g.hp
		if hp > 100:
			hp_bar = rgb_idx(120, 220, 255)
		elif hp > 60:
			hp_bar = rgb_idx(110, 220, 90)
		elif hp > 30:
			hp_bar = rgb_idx(240, 190, 60)
		else:
			hp_bar = rgb_idx(240, 60, 40)
		self.stat_block(c, x_hp, stat_w, "HEALTH", hp, min(hp, 100), 100, red_digits, hp_bar)
		self.stat_block(c, x_armor, stat_w, "ARMOR", g.armor, g.armor, 100, blue_digits, rgb_idx(90, 150, 240))

		c.rect(x_face, 2, 12, 10, rgb_idx(18, 16, 22))
		face = hud.face_name()
		c.art(self.faces[face], x_face + 1, 2, self.face_luts[face])

		weapon = WEAPONS[g.weapon]
		pool = weapon["ammo"]
		pool_colors = (rgb_idx(226, 186, 84), rgb_idx(226, 96, 64), rgb_idx(90, 230, 220))
		self.stat_block(c, x_ammo, stat_w, weapon["name"], g.ammo[pool], g.ammo[pool], AMMO_MAX[pool], gold_digits,
			pool_colors[pool])

		arms_w = w - x_arms - 1
		if arms_w >= 11:
			c.text(x_arms + 1, 1, "ARMS", dim)
			for slot in range(3):
				sx = x_arms + 1 + slot * 4
				owned = g.have[slot] == 1
				if slot == g.weapon:
					c.rect(sx, 4, 3, 2, rgb_idx(132, 100, 40))
				c.text(sx + 1, 2, str(slot + 1), rgb_idx(250, 240, 210) if owned else rgb_idx(70, 66, 76))
			if arms_w >= 26:
				c.text(x_arms + 14, 2, f"B{g.ammo[0]} S{g.ammo[1]} C{g.ammo[2]}", dim)
			self.key_chip(c, x_arms + 1, 3, "RED", g.key_r, rgb_idx(200, 40, 34))
			self.key_chip(c, x_arms + 7, 3, "BLUE", g.key_b, rgb_idx(44, 90, 210))
			if g.autopilot:
				c.text(x_arms + 1, 5, "AUTOPILOT", rgb_idx(110, 230, 230))
			elif fps > 0.0:
				cores = f"  {state.HOST_WORKERS} cores" if state.HOST_WORKERS > 0 else ""
				c.text(x_arms + 1, 5, f"{fps:.0f} fps {ms_sim + ms_draw:.1f} ms{cores}", dim)
		return c.rows(0, 6)

	def stat_block(self, c, x, width, label, value, bar, bar_max, digits, bar_color):
		"""a labelled big number with a bar beneath it"""
		c.text(x + 1, 1, label, rgb_idx(150, 140, 132))
		number = str(value)
		c.lettering(number, x + width - 1 - lettering_width(number, 1), 4, 1, digits, rgb_idx(16, 12, 14))
		bar_w = width - 2
		filled = iclamp(idivt(bar * bar_w, max(1, bar_max)), 0, bar_w)
		c.rect(x + 1, 10, bar_w, 1, rgb_idx(40, 36, 44))
		c.rect(x + 1, 10, filled, 1, bar_color)

	def key_chip(self, c, x, row, name, held, color):
		if held:
			c.rect(x, row * 2, len(name) + 2, 2, color)
			c.text(x + 1, row, name, rgb_idx(255, 255, 255))
		else:
			c.text(x + 1, row, name, rgb_idx(76, 70, 82))

	# -------------------------------------------------------------- pages ----
	def draw_fire(self, c, rows):
		"""each cell takes the average of the cells under it, a little cooler, over a
		bottom row of flickering embers"""
		g = state.G
		w = c.w
		height = c.h * 2
		if len(self.fire_colors) == 0:
			stops = (rgb(0, 0, 0), rgb(40, 6, 6), rgb(120, 16, 8), rgb(196, 48, 10), rgb(236, 110, 20),
				rgb(250, 176, 50), rgb(255, 228, 120), rgb(255, 250, 220))
			for i in range(36):
				t = i / 35.0 * (len(stops) - 1)
				k = int(t)
				k2 = min(len(stops) - 1, k + 1)
				self.fire_colors.append(color_idx(mix_rgb(stops[k], stops[k2], t - k)))
		if len(self.heat) != w * (rows + 2):
			self.heat = [0] * (w * (rows + 2))
			self.fire_tick = -1
		steps = rows * 2 if self.fire_tick < 0 else min(4, g.tick - self.fire_tick)
		start = 0 if self.fire_tick < 0 else self.fire_tick
		for s in range(steps):
			self.fire_step(w, rows + 2, start + s)
		self.fire_tick = g.tick
		top = height - rows
		heat = self.heat
		px = c.px
		colors = self.fire_colors
		for y in range(rows):
			for x in range(w):
				h = heat[y * w + x] * 35 // 255
				if h > 2:
					px[(top + y) * w + x] = colors[h]

	def fire_step(self, w, rows, tick):
		heat = self.heat
		for r in range(rows - 2, rows):
			for x in range(w):
				n = (mix32(mix32(tick, x), 17 + r) >> 9) & 255
				heat[r * w + x] = 60 if n < 90 else 255
		cool = 2 + 230 // max(1, rows)
		for y in range(rows - 2):
			row = y * w
			for x in range(w):
				below = row + w + x
				left = heat[below - 1] if x > 0 else heat[below]
				right = heat[below + 1] if x < w - 1 else heat[below]
				h = (left + heat[below] + right + heat[below + w]) // 4 - cool
				heat[row + x] = 0 if h < 0 else h

	def draw_embers(self, c, count, hot, cool):
		"""sparks rising from the bottom of the page"""
		tick = state.G.tick
		height = c.h * 2
		period = height + 16
		for i in range(count):
			speed = 1 + ember(i, 1) % 3
			x = ember(i, 2) % c.w
			rise = (tick * speed // 2 + ember(i, 3) % period) % period
			y = height - rise
			if (ember(i, tick // 4) & 3) == 0:
				continue
			t = 1.0 * rise / period
			c.dot(x + ((rise // 6 + i) % 3) - 1, y, color_idx(mix_rgb(hot, cool, t)))

	def heading(self, c, s, y, max_scale, top_bottom, shadow):
		"""a heading in the pixel font, centered at pixel row y, at the largest scale that fits"""
		scale = max_scale
		while scale > 1 and lettering_width(s, scale) > c.w - 4:
			scale -= 1
		c.lettering(s, idivt(c.w - lettering_width(s, scale), 2), y, scale, top_bottom, shadow)
		return 5 * scale

	def draw_logo(self, c, x, y, top, bottom, bleeding):
		"""outlined, shadowed, lit from above; a bleeding logo gathers drops under its letters that fall"""
		lw = len(LOGO_ART[0])
		lh = len(LOGO_ART)
		outline = rgb_idx(6, 2, 4)
		shadow = rgb_idx(40, 6, 8)
		for ly in range(lh):
			for lx in range(lw):
				if LOGO_ART[ly][lx] != "#":
					continue
				c.rect(x + lx - 1, y + ly - 1, 3, 3, outline)
		for ly in range(lh):
			for lx in range(lw):
				if LOGO_ART[ly][lx] == "#":
					c.dot(x + lx + 1, y + ly + 2, shadow)
		for ly in range(lh):
			fill = color_idx(mix_rgb(top, bottom, 1.0 * ly / (lh - 1)))
			for lx in range(lw):
				if LOGO_ART[ly][lx] == "#":
					c.dot(x + lx, y + ly, fill)
		if not bleeding:
			return
		blood = rgb_idx(150, 14, 12)
		drop = rgb_idx(210, 28, 22)
		tick = state.G.tick
		for lx in range(lw):
			if LOGO_ART[lh - 1][lx] != "#" or ember(lx, 21) % 4 != 0:
				continue
			length = 1 + ember(lx, 22) % 4
			c.rect(x + lx, y + lh, 1, length, blood)
			fall = (tick + ember(lx, 23) % 60) % 60
			if fall < 24:
				c.dot(x + lx, y + lh + length + fall // 2, drop)

	def title_page(self):
		g = state.G
		c = self.page
		c.clear(0)
		height = c.h * 2
		glow_at = height * 3 // 5
		c.gradient(0, glow_at, rgb(6, 5, 9), rgb(22, 8, 12))
		c.gradient(glow_at, height, rgb(22, 8, 12), rgb(84, 26, 12))
		self.draw_fire(c, height * 2 // 5)
		self.draw_embers(c, c.w // 2, rgb(255, 214, 120), rgb(110, 30, 16))

		flicker = ember(g.tick // 3, 5) % 9 == 0
		bone = rgb(190, 180, 160) if flicker else rgb(248, 238, 216)
		logo_y = max(2, height // 2 - 24)
		self.draw_logo(c, idivt(c.w - len(LOGO_ART[0]), 2), logo_y, bone, rgb(160, 24, 20), True)
		row = (logo_y + len(LOGO_ART)) // 2 + 4
		c.text_centered(row, "a terminal raycaster, written in JaiScript", rgb_idx(186, 154, 130))
		if blink(12):
			c.text_centered(row + 3, "-  press FIRE to descend  -", rgb_idx(255, 226, 160))
		keys = rgb_idx(250, 214, 150)
		words = rgb_idx(150, 132, 126)
		self.controls_line(c, row + 6, ("W S", "move", "A D", "strafe", "LEFT RIGHT", "turn", "SHIFT", "run"), keys, words)
		self.controls_line(c, row + 7, ("SPACE", "fire", "E", "use", "1 2 3", "weapons", "M", "map", "ESC", "quit"),
			keys, words)
		return c.rows(0, c.h)

	def controls_line(self, c, row, pairs, key_color, word_color):
		"""'key word   key word ...' centered, keys bright, words dim"""
		width = 0
		for i in range(0, len(pairs), 2):
			width += len(pairs[i]) + 1 + len(pairs[i + 1]) + 4
		x = idivt(c.w - (width - 4), 2)
		for i in range(0, len(pairs), 2):
			c.text(x, row, pairs[i], key_color)
			x += len(pairs[i]) + 1
			c.text(x, row, pairs[i + 1], word_color)
			x += len(pairs[i + 1]) + 4

	def tally_page(self):
		"""the intermission: the floor's numbers count up, DOOM style"""
		g = state.G
		c = self.page
		c.clear(0)
		mood = MOODS[g.map_i]
		height = c.h * 2
		c.gradient(0, height, mix_rgb(mood.fog, rgb(0, 0, 0), 0.3), mix_rgb(mood.ceiling, mood.light, 0.12))
		gold = shades5(rgb(255, 246, 200), rgb(220, 150, 60))
		top = max(2, height // 2 - 22)
		c.text_centered(top // 2, g.map_name, color_idx(mood.light))
		head_h = self.heading(c, "FLOOR CLEARED", top + 4, 2, gold, rgb_idx(8, 6, 8))
		row = (top + 4 + head_h) // 2 + 3
		counted = g.mode_t * 2
		kills = min(g.kills, counted // 3)
		secrets = min(g.secrets, counted // 9)
		time = min(g.map_ticks, counted * 40)
		kp = kills * 100 // g.kill_total if g.kill_total > 0 else 100
		sp = secrets * 100 // g.secret_total if g.secret_total > 0 else 100
		label = rgb_idx(170, 160, 150)
		value = rgb_idx(255, 236, 190)
		x = c.w // 2 - 17
		self.tally_row(c, x, row, "KILLS", f"{kills} / {g.kill_total}", kp, label, value)
		self.tally_row(c, x, row + 2, "SECRETS", f"{secrets} / {g.secret_total}", sp, label, value)
		c.text(x, row + 4, "TIME", label)
		c.text(x + 10, row + 4, clock_text(time), value)
		c.text(x + 18, row + 4, f"PAR {clock_text(g.par)}", label)
		done = kills == g.kills and secrets == g.secrets and time == g.map_ticks
		if done and blink(12):
			nxt = "press FIRE for the next floor" if g.map_i + 1 < len(MAPS) else "press FIRE to face the dark"
			c.text_centered(row + 8, nxt, rgb_idx(255, 226, 160))
		return c.rows(0, c.h)

	def tally_row(self, c, x, row, name, count, percent, label, value):
		c.text(x, row, name, label)
		c.text(x + 10, row, count, value)
		c.text_right(x + 24, row, f"{percent}%", value)
		c.rect(x + 26, row * 2, 10, 2, rgb_idx(40, 34, 40))
		c.rect(x + 26, row * 2, percent // 10, 2, rgb_idx(120, 230, 110) if percent == 100 else rgb_idx(226, 176, 70))

	def dead_page(self):
		g = state.G
		c = self.page
		c.clear(0)
		height = c.h * 2
		c.gradient(0, height, rgb(36, 4, 6), rgb(8, 2, 3))
		# blood runs down from the top, each run at its own pace and to its own depth
		trail = rgb_idx(96, 8, 8)
		bead = rgb_idx(214, 34, 28)
		for x in range(c.w):
			if ember(x, 10) % 3 != 0:
				continue
			depth = height // 4 + ember(x, 13) % (height // 2)
			length = min(depth, g.mode_t * (2 + ember(x, 11) % 5) // 6 - ember(x, 12) % 12)
			for y in range(length):
				c.dot(x, y, bead if y == length - 1 else trail)
		red = shades5(rgb(255, 120, 100), rgb(150, 10, 10))
		top = height // 2 - 14
		head_h = self.heading(c, "YOU DIED", top, 2, red, rgb_idx(6, 0, 0))
		row = (top + head_h) // 2 + 2
		c.text_centered(row, f"slain by {g.death_cause} on {g.map_name}", rgb_idx(236, 200, 190))
		c.text_centered(row + 2, f"kills {g.kills}/{g.kill_total}    deaths {g.deaths}", rgb_idx(170, 130, 126))
		if g.mode_t > 25 and blink(12):
			c.text_centered(row + 5, "press FIRE to rise again", rgb_idx(255, 226, 160))
		return c.rows(0, c.h)

	def victory_page(self):
		g = state.G
		c = self.page
		c.clear(0)
		height = c.h * 2
		c.gradient(0, height, rgb(10, 8, 14), rgb(70, 52, 20))
		self.draw_embers(c, c.w // 3, rgb(255, 240, 170), rgb(120, 90, 30))
		top = max(2, height // 2 - 24)
		self.draw_logo(c, idivt(c.w - len(LOGO_ART[0]), 2), top, rgb(255, 250, 220), rgb(214, 140, 40), False)
		row = (top + len(LOGO_ART)) // 2 + 4
		c.text_centered(row, "T H E   W A R D E N   I S   D O W N", rgb_idx(255, 230, 160))
		label = rgb_idx(186, 170, 150)
		bright = rgb_idx(255, 246, 210)
		x = c.w // 2 - 14
		c.text(x, row + 3, "EPISODE KILLS", label)
		c.text(x + 18, row + 3, f"{g.ep_kills} / {g.ep_kill_total}", bright)
		c.text(x, row + 4, "SECRETS", label)
		c.text(x + 18, row + 4, f"{g.ep_secrets} / {g.ep_secret_total}", bright)
		c.text(x, row + 5, "TIME", label)
		c.text(x + 18, row + 5, clock_text(g.ep_ticks), bright)
		c.text(x, row + 6, "DEATHS", label)
		c.text(x + 18, row + 6, f"{g.deaths}", bright)
		c.text_centered(row + 9, "the gloom lifts. for now.      ESC quits", rgb_idx(170, 150, 120))
		return c.rows(0, c.h)
