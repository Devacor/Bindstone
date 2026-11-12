"""The showcase look (look.jai): floor moods, wall, floor and ceiling textures,
the lit color tables, sprite shading and particle colors, all built at boot and
on map load; the renderer's per-pixel work only looks them up. Colors are
binary32 math::color tuples (colors.py)."""

from math import sqrt
from typing import NamedTuple

import state
from colors import cadd, cclamp, cmul, color_idx, cscale, mix_rgb, rgb
from defs import ARTS
from lights import LIGHT_LAMP, LIGHT_LEVELS, LIGHT_VARIANTS, light_level
from tune import AMBIENT, LIGHT_COLORS, LIGHT_GAIN
from util import mix32, rgb_idx


class Mood(NamedTuple):
	"""a floor's lighting: the torchlight that tints what is near, the haze that
	distance fades into, the tint a wall turned from the light takes, the surfaces"""
	light: tuple
	fog: tuple
	turned: tuple
	ceiling: tuple
	floor: tuple
	walls: tuple    # wall kinds 1-4


MOODS = (
	# E1M1: torchlit sandstone, violet shadows
	Mood(rgb(255, 230, 196), rgb(22, 16, 26), rgb(120, 110, 170), rgb(58, 46, 54), rgb(96, 80, 60),
		(rgb(156, 136, 112), rgb(108, 128, 92), rgb(104, 110, 136), rgb(152, 100, 80))),
	# E1M2: coolant glow, teal haze
	Mood(rgb(190, 236, 255), rgb(6, 20, 26), rgb(70, 120, 150), rgb(26, 46, 56), rgb(44, 70, 72),
		(rgb(100, 140, 152), rgb(84, 112, 164), rgb(104, 152, 116), rgb(136, 114, 152))),
	# E1M3: furnace heat, ember haze
	Mood(rgb(255, 196, 140), rgb(34, 12, 8), rgb(150, 70, 60), rgb(70, 34, 24), rgb(86, 60, 42),
		(rgb(176, 114, 70), rgb(144, 86, 118), rgb(132, 124, 72), rgb(96, 130, 140))),
	# E1M4: the throne's red dark
	Mood(rgb(255, 170, 150), rgb(30, 6, 8), rgb(110, 40, 70), rgb(58, 18, 20), rgb(70, 42, 36),
		(rgb(176, 72, 58), rgb(122, 54, 48), rgb(154, 130, 78), rgb(92, 66, 80))),
)

# wall texels name one of eight tones; a tone is a color per (side, shade)
TONE_BASE = 0
TONE_ACCENT = 1
TONE_GAP = 2
TONE_LIT_EDGE = 3
TONE_DARK_EDGE = 4
TONE_GLOW = 5           # emissive: lamps and lock lights stay bright in the dark
TONE_GRIT_DARK = 6
TONE_GRIT_LIGHT = 7
TONES = 8


class Fixture(NamedTuple):
	"""the surfaces every floor shares: doors, the exit switch, the keyed doors"""
	base: tuple
	accent: tuple
	glow: tuple


FIXTURES = (
	Fixture(rgb(128, 108, 90), rgb(170, 144, 110), rgb(255, 210, 140)),   # 5 door
	Fixture(rgb(62, 66, 60), rgb(226, 190, 60), rgb(110, 255, 130)),      # 6 exit switch
	Fixture(rgb(118, 62, 56), rgb(176, 92, 72), rgb(255, 70, 50)),        # 7 red door
	Fixture(rgb(62, 76, 124), rgb(94, 114, 176), rgb(80, 156, 255)),      # 8 blue door
)


def shade_light(shade):
	"""distance shade 0 (far) .. 15 (near) -> light reaching the wall"""
	t = shade / 15.0
	return 0.05 + 0.8 * t * sqrt(t)


def texel_noise(kind, tx, ty):
	"""deterministic texel noise: 0..255"""
	return mix32(mix32(kind * 7919 + 17, tx), ty * 31 + kind) & 255


def grit(kind, tx, ty):
	n = texel_noise(kind, tx, ty)
	if n < 26:
		return TONE_GRIT_DARK
	if n > 236:
		return TONE_GRIT_LIGHT
	return TONE_BASE


def showcase_tone(kind, tx, ty):
	"""the showcase texture of a wall kind: tone at texel (tx, ty), both 0..31"""
	if kind == 1:
		return stone_tone(tx, ty)
	if kind == 2:
		return panel_tone(tx, ty)
	if kind == 3:
		return conduit_tone(tx, ty)
	if kind == 4:
		return block_tone(tx, ty)
	if kind == 6:
		return switch_tone(tx, ty)
	if kind >= 5:
		return door_tone(kind, tx, ty)
	return TONE_BASE


def stone_tone(tx, ty):
	"""staggered bricks, eight texels tall"""
	row = ty >> 3
	y = ty & 7
	x = (tx + (row & 1) * 8) & 15
	if y == 7 or x == 15:
		return TONE_GAP
	if y == 0 or x == 0:
		return TONE_LIT_EDGE
	if y == 6:
		return TONE_DARK_EDGE
	if ((row * 5 + ((tx + (row & 1) * 8) >> 4)) % 3) == 0:
		return TONE_ACCENT
	return grit(1, tx, ty)


def panel_tone(tx, ty):
	"""riveted plates with a painted band"""
	x = tx & 15
	y = ty & 15
	if x == 15 or y == 15:
		return TONE_GAP
	if x == 0 or y == 0:
		return TONE_LIT_EDGE
	if x == 14 or y == 14:
		return TONE_DARK_EDGE
	if (x == 2 or x == 12) and (y == 2 or y == 12):
		return TONE_LIT_EDGE
	if (x == 2 or x == 12) and (y == 3 or y == 13):
		return TONE_DARK_EDGE
	if 6 <= y <= 8:
		return TONE_ACCENT
	return grit(2, tx, ty)


def conduit_tone(tx, ty):
	"""runs of pipe held by brackets"""
	y = ty & 7
	if (tx & 15) < 2:
		if y == 0:
			return TONE_LIT_EDGE
		return TONE_ACCENT if (tx & 15) == 0 else TONE_DARK_EDGE
	if y == 0:
		return TONE_LIT_EDGE
	if y == 5:
		return TONE_DARK_EDGE
	if y >= 6:
		return TONE_GAP
	if y == 1:
		return TONE_ACCENT
	return grit(3, tx, ty)


def block_tone(tx, ty):
	"""rough cut blocks, cracked and mossy"""
	row = ty >> 4
	y = ty & 15
	x = (tx + row * 11) & 31
	if y == 15 or x == 31:
		return TONE_GAP
	if y == 0 or x == 0:
		return TONE_LIT_EDGE
	if y == 14 or x == 30:
		return TONE_DARK_EDGE
	n = texel_noise(4, tx, ty)
	if (tx + ty * 2 + n % 3) % 23 == 0:
		return TONE_GAP
	if y > 9 and n % 5 == 0:
		return TONE_ACCENT
	return grit(4, tx, ty)


def door_tone(kind, tx, ty):
	"""a framed door of planks, banded, with a handle; keyed doors carry lock lights"""
	if tx < 2 or tx > 29 or ty < 1:
		return TONE_DARK_EDGE
	if ty > 29:
		return TONE_GAP
	if tx == 2 or tx == 29 or ty == 1:
		return TONE_LIT_EDGE
	if tx == 15 or tx == 16:
		return TONE_GAP
	if kind != 5 and (tx == 6 or tx == 7 or tx == 24 or tx == 25) and 4 < ty < 27:
		return TONE_GLOW
	if ty == 7 or ty == 22:
		return TONE_LIT_EDGE
	if ty == 8 or ty == 9 or ty == 23 or ty == 24:
		return TONE_ACCENT
	if 14 <= ty <= 17 and (tx == 13 or tx == 18):
		return TONE_LIT_EDGE
	if tx % 5 == 0:
		return TONE_DARK_EDGE
	return grit(kind, tx, ty)


def switch_tone(tx, ty):
	"""the exit: a switch box over hazard stripes, two green lamps"""
	if tx < 2 or tx > 29 or ty < 2:
		return TONE_DARK_EDGE
	if ty >= 24:
		return TONE_ACCENT if ((tx + ty) >> 2 & 1) == 0 else TONE_GAP
	if 9 <= tx <= 22 and 5 <= ty <= 20:
		if tx == 9 or ty == 5:
			return TONE_LIT_EDGE
		if tx == 22 or ty == 20:
			return TONE_GAP
		if 15 <= tx <= 16 and 7 <= ty <= 14:
			return TONE_LIT_EDGE
		if (tx == 12 or tx == 19) and 16 <= ty <= 18:
			return TONE_GLOW
		return TONE_GRIT_DARK
	return grit(6, tx, ty)


# --------------------------------------------------------------- sprites ----
_WHITE = rgb(255, 255, 255)
_BLACK = rgb(0, 0, 0)


def showcase_sprite_lut(name, band, flash, variant):
	"""sprites keep more light than walls at the same distance; uppercase palette
	keys (eyes, gobs, flame) and every projectile glow at full brightness"""
	mood = MOODS[state.G.map_i]
	art = ARTS[name]
	glowing = name[:3] == "pr_"
	lum = 1.0 - band * 0.085
	glow = sprite_light(mood, variant)
	light = cadd(cscale(mix_rgb(mood.light, _WHITE, 0.55), lum), glow)
	haze = cscale(mood.fog, (1.0 - lum) * 0.8 * (1.0 - 0.15 * light_level(variant)))
	lut = [0]
	keys = art.keys
	for c in range(1, len(art.rgb)):
		r, g, b = art.rgb[c]
		if flash == 1:
			r = min(255, r + 170)
			g = min(255, g + 150)
			b = min(255, b + 140)
		col = rgb(r, g, b)
		key = keys[c]
		if glowing or "A" <= key <= "Z":
			lut.append(color_idx(col))
		else:
			lut.append(color_idx(cadd(cmul(col, light), haze)))
	return lut


# per-kind particle ramps, phase 0 = fresh
SHOWCASE_PARTICLE_RAMPS = (
	(rgb(0, 0, 0), rgb(0, 0, 0), rgb(0, 0, 0), rgb(0, 0, 0)),
	(rgb(255, 250, 214), rgb(255, 212, 110), rgb(240, 140, 50), rgb(150, 70, 30)),      # spark
	(rgb(232, 40, 30), rgb(190, 30, 24), rgb(140, 22, 18), rgb(90, 14, 12)),            # blood
	(rgb(214, 72, 54), rgb(172, 50, 38), rgb(128, 36, 28), rgb(86, 24, 20)),            # gib
	(rgb(104, 98, 94), rgb(84, 80, 78), rgb(64, 62, 62), rgb(46, 44, 46)),              # smoke
	(rgb(255, 255, 236), rgb(255, 240, 170), rgb(255, 200, 100), rgb(220, 130, 50)),    # flash
	(rgb(190, 255, 200), rgb(130, 245, 220), rgb(100, 200, 255), rgb(110, 120, 240)),   # hexmote
)

# ----------------------------------------------------------------- light ----
# The lit tables: one palette index per (light variant, side, shade, tone), so a
# lit surface costs one lookup more than an unlit one. Variant 0 is unlit.
SURFACE_SHADES = 16
FAR_TEXTURE = 3.2       # past this distance surfaces draw their far texture
FLOOR_TONES = 12        # the eight wall tones, then blood, dried blood, scorch, ash
TONE_BLOOD = 8
TONE_BLOOD_DRY = 9
TONE_SCORCH = 10
TONE_ASH = 11

# the lit tables' colors, rounded to steps of LIT_STEP a channel
LIT_STEP = 6


def lit_idx(c):
	half = LIT_STEP // 2
	r = (int(c[0] * 255.0) + half) // LIT_STEP * LIT_STEP
	g = (int(c[1] * 255.0) + half) // LIT_STEP * LIT_STEP
	b = (int(c[2] * 255.0) + half) // LIT_STEP * LIT_STEP
	return rgb_idx(r, g, b)


def light_color_of(mood, variant):
	channel = (variant - 1) // LIGHT_LEVELS
	if channel == LIGHT_LAMP:
		return mix_rgb(mood.light, LIGHT_COLORS[LIGHT_LAMP], 0.5)
	return LIGHT_COLORS[channel]


def push_lit_tones(out, mood, albedo, glow, sides):
	"""[variant][side][shade][tone] palette indices for one surface, appended to out"""
	append = out.append
	for v in range(LIGHT_VARIANTS):
		s = 0.0 if v == 0 else 1.0 * light_level(v) / LIGHT_LEVELS
		lc = _BLACK if v == 0 else light_color_of(mood, v)
		for side in range(sides):
			light = cscale(cscale(mix_rgb(mood.light, mood.turned, 0.3), 0.7) if side == 1 else mood.light, AMBIENT)
			for shade in range(SURFACE_SHADES):
				lum = shade_light(shade)
				haze = cscale(mood.fog, 1.0 - lum)
				lit = cadd(cscale(light, lum), cscale(lc, s * LIGHT_GAIN * (0.45 + 0.55 * lum)))
				fog = cscale(haze, 1.0 - 0.6 * s)
				for tone in range(len(albedo)):
					if tone == TONE_GLOW:
						append(lit_idx(cadd(cscale(mix_rgb(glow, _WHITE, 0.15), 0.6 + 0.4 * lum), cscale(haze, 0.3))))
					else:
						append(lit_idx(cadd(cmul(albedo[tone], lit), fog)))


def wall_albedo(base, accent):
	return [base, accent, cscale(base, 0.36), cclamp(cscale(base, 1.25), 0.0, 1.0), cscale(base, 0.62), base,
		cscale(base, 0.86), cclamp(cscale(base, 1.1), 0.0, 1.0)]


WALL_TABLE_STRIDE = 6400    # LIGHT_VARIANTS x 2 sides x 16 shades x 8 tones


def showcase_lit_wall_tones(mi):
	"""the lit wall table of floor mi: [kind 0..8][variant][side][shade][tone]"""
	mood = MOODS[mi]
	tones = []
	unseen = rgb(10, 10, 12)
	push_lit_tones(tones, mood, wall_albedo(unseen, unseen), unseen, 2)
	for wall in mood.walls:
		push_lit_tones(tones, mood, wall_albedo(wall, mix_rgb(wall, mood.light, 0.18)), wall, 2)
	for fixture in FIXTURES:
		push_lit_tones(tones, mood, wall_albedo(fixture.base, fixture.accent), fixture.glow, 2)
	return tones


# what each floor is paved with, and what glows in its cracks
FLOOR_GLOWS = (rgb(255, 210, 140), rgb(90, 236, 226), rgb(255, 122, 40), rgb(255, 60, 44))


def showcase_floor_tones(mi):
	mood = MOODS[mi]
	base = cclamp(cscale(mood.floor, 1.3), 0.0, 1.0)
	accent = mix_rgb(base, mood.walls[0], 0.35)
	albedo = wall_albedo(base, accent)
	albedo[TONE_GAP] = cscale(base, 0.55)
	albedo[TONE_GRIT_DARK] = cscale(base, 0.9)
	albedo[TONE_GRIT_LIGHT] = cclamp(cscale(base, 1.06), 0.0, 1.0)
	albedo.append(rgb(150, 16, 12))
	albedo.append(rgb(84, 10, 10))
	albedo.append(rgb(22, 18, 16))
	albedo.append(rgb(52, 46, 44))
	tones = []
	push_lit_tones(tones, mood, albedo, FLOOR_GLOWS[mi], 1)
	return tones


def showcase_ceiling_tones(mi):
	mood = MOODS[mi]
	base = cscale(mood.ceiling, 0.85)
	albedo = wall_albedo(base, mix_rgb(base, mood.light, 0.12))
	albedo[TONE_GRIT_DARK] = cscale(base, 0.92)
	albedo[TONE_GRIT_LIGHT] = base
	tones = []
	push_lit_tones(tones, mood, albedo, mix_rgb(mood.light, _WHITE, 0.45), 1)
	return tones


# ------------------------------------------------------ floor and ceiling ----
# 16x16 texels a tile; four floor variants, picked per tile from a hash. Each
# texture comes twice: as drawn, then as seen from afar (only glow, tint and
# beams stay, the one-texel lines and the grit would shimmer)
def far_tone(tone, keep_edges):
	if tone == TONE_GLOW or tone == TONE_ACCENT:
		return tone
	if keep_edges and (tone == TONE_GAP or tone == TONE_DARK_EDGE):
		return tone
	return TONE_BASE


def showcase_floor_texture(mi):
	tex = []
	for far in range(2):
		for variant in range(4):
			for v in range(16):
				for u in range(16):
					tone = floor_tone(mi, variant, u, v)
					if far == 1:
						if mi == 1:
							tone = far_tone(tone, False)
						else:
							tone = TONE_DARK_EDGE if tone == TONE_GLOW else far_tone(tone, False)
					tex.append(tone)
	return tex


def floor_tone(mi, variant, u, v):
	n = texel_noise(20 + variant, u, v)
	if mi == 1:
		# coolant grating: plates over a lit channel
		if u == 0 or v == 0:
			return TONE_LIT_EDGE
		if u == 15 or v == 15:
			return TONE_GAP
		if 6 <= v <= 9 and 2 <= u <= 13 and (u & 1) == 0:
			return TONE_GLOW if variant == 0 else TONE_GAP
		if (u == 3 or u == 12) and (v == 3 or v == 12):
			return TONE_LIT_EDGE
		return TONE_GRIT_DARK if n < 30 else TONE_BASE
	if mi == 2:
		# foundry basalt: blocks split by cracks that glow
		x = (u + (v >> 3) * 5) & 15
		y = v & 7
		crack_at = (v * 3) // 4 + 3 + (v >> 2 & 1) if variant == 0 else 99
		if u == crack_at or (variant == 0 and u == crack_at + 1 and (v & 3) == 0):
			return TONE_GLOW
		if x == 15 or y == 7:
			return TONE_GAP
		if x == 0 or y == 0:
			return TONE_LIT_EDGE
		if n < 40:
			return TONE_GRIT_DARK
		return TONE_ACCENT if variant == 3 else TONE_BASE
	if mi == 3:
		# the throne: checkered tiles trimmed in gold
		if u == 0 or v == 0 or u == 8 or v == 8:
			return TONE_LIT_EDGE
		dark = ((u >> 3) + (v >> 3)) % 2 == 0
		if dark:
			return TONE_GRIT_DARK if n < 50 else TONE_DARK_EDGE
		return TONE_GRIT_LIGHT if n < 30 else TONE_ACCENT
	# flagstones, four to a tile, each its own tint
	sx = u >> 3
	sy = v >> 3
	x = u & 7
	y = v & 7
	if x == 7 or y == 7:
		return TONE_GAP
	if x == 0 or y == 0:
		return TONE_LIT_EDGE
	if x == 6 or y == 6:
		return TONE_DARK_EDGE
	if ((sx + sy * 2 + variant) % 3) == 0:
		return TONE_GRIT_DARK if n < 24 else TONE_ACCENT
	if n < 26:
		return TONE_GRIT_DARK
	if n > 238:
		return TONE_GRIT_LIGHT
	return TONE_BASE


def showcase_ceiling_texture():
	"""ceiling panels between beams; variant 1 is a lamp"""
	tex = []
	for far in range(2):
		for lamp in range(2):
			for v in range(16):
				for u in range(16):
					tone = ceiling_tone(lamp == 1, u, v)
					tex.append(far_tone(tone, True) if far == 1 else tone)
	return tex


def ceiling_tone(lamp, u, v):
	if u < 2 or v < 2:
		return TONE_GAP if u == 1 or v == 1 else TONE_DARK_EDGE
	if lamp and 5 <= u <= 12 and 5 <= v <= 12:
		if u == 5 or v == 5 or u == 12 or v == 12:
			return TONE_LIT_EDGE
		return TONE_GLOW
	if u == 2 or v == 2:
		return TONE_LIT_EDGE
	return grit(9, u, v)


def showcase_wall_texture():
	"""every wall kind's texture as tones, 32x32 texels: [far][kind][ty][tx]"""
	tex = []
	for far in range(2):
		for kind in range(9):
			for ty in range(32):
				for tx in range(32):
					tone = showcase_tone(kind, tx, ty)
					tex.append(far_tone(tone, True) if far == 1 else tone)
	return tex


def sprite_light(mood, variant):
	"""how a light variant tints a sprite"""
	if variant == 0:
		return _BLACK
	return cscale(light_color_of(mood, variant), 0.9 * light_level(variant) / LIGHT_LEVELS)
