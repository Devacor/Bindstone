-- The showcase look: how GLOOM is lit and colored when it is played. Floor
-- moods, wall, floor and ceiling textures, the lit color tables, sprite shading
-- and particle colors are all built at boot and on map load; the renderer's
-- per-pixel work only looks them up. The reference look keeps its own tables,
-- so --smoke keeps the frames the ports reproduce. Colors are the binary32
-- {r, g, b} triples of util.lua, mixed per channel.

SHOWCASE = false   -- gloom_boot: HOST_LOOK == "showcase"

local sqrt = math.sqrt

function rgb(r, g, b)
	return color(r / 255.0, g / 255.0, b / 255.0)
end

function color_idx(c)
	return rgb_idx(trunc(c[1] * 255.0 + 0.5), trunc(c[2] * 255.0 + 0.5), trunc(c[3] * 255.0 + 0.5))
end

-- A floor's lighting: the torchlight that tints what is near, the haze that
-- distance fades into (also the horizon of floor and ceiling), the tint a
-- wall turned away from the light takes, and the surfaces themselves.
local function mood(light, fog, turned, ceiling, floor_, walls)
	return {light = light, fog = fog, turned = turned, ceiling = ceiling, floor = floor_, walls = walls}
end

MOODS = {   -- [map_i + 1]
	-- E1M1: torchlit sandstone, violet shadows
	mood(rgb(255, 230, 196), rgb(22, 16, 26), rgb(120, 110, 170), rgb(58, 46, 54), rgb(96, 80, 60),
		{rgb(156, 136, 112), rgb(108, 128, 92), rgb(104, 110, 136), rgb(152, 100, 80)}),
	-- E1M2: coolant glow, teal haze
	mood(rgb(190, 236, 255), rgb(6, 20, 26), rgb(70, 120, 150), rgb(26, 46, 56), rgb(44, 70, 72),
		{rgb(100, 140, 152), rgb(84, 112, 164), rgb(104, 152, 116), rgb(136, 114, 152)}),
	-- E1M3: furnace heat, ember haze
	mood(rgb(255, 196, 140), rgb(34, 12, 8), rgb(150, 70, 60), rgb(70, 34, 24), rgb(86, 60, 42),
		{rgb(176, 114, 70), rgb(144, 86, 118), rgb(132, 124, 72), rgb(96, 130, 140)}),
	-- E1M4: the throne's red dark
	mood(rgb(255, 170, 150), rgb(30, 6, 8), rgb(110, 40, 70), rgb(58, 18, 20), rgb(70, 42, 36),
		{rgb(176, 72, 58), rgb(122, 54, 48), rgb(154, 130, 78), rgb(92, 66, 80)})
}

-- Wall texels name one of eight tones; a tone is a color per (side, shade).
TONE_BASE = 0
TONE_ACCENT = 1
TONE_GAP = 2
TONE_LIT_EDGE = 3
TONE_DARK_EDGE = 4
TONE_GLOW = 5      -- emissive: lamps and lock lights stay bright in the dark
TONE_GRIT_DARK = 6
TONE_GRIT_LIGHT = 7
TONES = 8

-- The surfaces every floor shares: doors, the exit switch, the keyed doors.
FIXTURES = {
	{base = rgb(128, 108, 90), accent = rgb(170, 144, 110), glow = rgb(255, 210, 140)},   -- 5 door
	{base = rgb(62, 66, 60), accent = rgb(226, 190, 60), glow = rgb(110, 255, 130)},      -- 6 exit switch
	{base = rgb(118, 62, 56), accent = rgb(176, 92, 72), glow = rgb(255, 70, 50)},        -- 7 red door
	{base = rgb(62, 76, 124), accent = rgb(94, 114, 176), glow = rgb(80, 156, 255)}       -- 8 blue door
}

-- distance shade 0 (far) .. 15 (near) -> light reaching the wall
function shade_light(shade)
	local t = shade / 15.0
	return 0.05 + 0.8 * t * sqrt(t)
end

-- deterministic texel noise: 0..255
function texel_noise(kind, tx, ty)
	return mix32(mix32(kind * 7919 + 17, tx), ty * 31 + kind) & 255
end

local function grit(kind, tx, ty)
	local n = texel_noise(kind, tx, ty)
	if n < 26 then return TONE_GRIT_DARK end
	if n > 236 then return TONE_GRIT_LIGHT end
	return TONE_BASE
end

-- staggered bricks, eight texels tall
local function stone_tone(tx, ty)
	local row = ty >> 3
	local y = ty & 7
	local x = (tx + (row & 1) * 8) & 15
	if y == 7 or x == 15 then return TONE_GAP end
	if y == 0 or x == 0 then return TONE_LIT_EDGE end
	if y == 6 then return TONE_DARK_EDGE end
	if (row * 5 + ((tx + (row & 1) * 8) >> 4)) % 3 == 0 then return TONE_ACCENT end
	return grit(1, tx, ty)
end

-- riveted plates with a painted band
local function panel_tone(tx, ty)
	local x = tx & 15
	local y = ty & 15
	if x == 15 or y == 15 then return TONE_GAP end
	if x == 0 or y == 0 then return TONE_LIT_EDGE end
	if x == 14 or y == 14 then return TONE_DARK_EDGE end
	if (x == 2 or x == 12) and (y == 2 or y == 12) then return TONE_LIT_EDGE end
	if (x == 2 or x == 12) and (y == 3 or y == 13) then return TONE_DARK_EDGE end
	if y >= 6 and y <= 8 then return TONE_ACCENT end
	return grit(2, tx, ty)
end

-- runs of pipe held by brackets
local function conduit_tone(tx, ty)
	local y = ty & 7
	if (tx & 15) < 2 then
		if y == 0 then return TONE_LIT_EDGE end
		if (tx & 15) == 0 then return TONE_ACCENT end
		return TONE_DARK_EDGE
	end
	if y == 0 then return TONE_LIT_EDGE end
	if y == 5 then return TONE_DARK_EDGE end
	if y >= 6 then return TONE_GAP end
	if y == 1 then return TONE_ACCENT end
	return grit(3, tx, ty)
end

-- rough cut blocks, cracked and mossy
local function block_tone(tx, ty)
	local row = ty >> 4
	local y = ty & 15
	local x = (tx + row * 11) & 31
	if y == 15 or x == 31 then return TONE_GAP end
	if y == 0 or x == 0 then return TONE_LIT_EDGE end
	if y == 14 or x == 30 then return TONE_DARK_EDGE end
	local n = texel_noise(4, tx, ty)
	if (tx + ty * 2 + n % 3) % 23 == 0 then return TONE_GAP end
	if y > 9 and n % 5 == 0 then return TONE_ACCENT end
	return grit(4, tx, ty)
end

-- a framed door of planks, banded, with a handle; keyed doors carry lock lights
local function door_tone(kind, tx, ty)
	if tx < 2 or tx > 29 or ty < 1 then return TONE_DARK_EDGE end
	if ty > 29 then return TONE_GAP end
	if tx == 2 or tx == 29 or ty == 1 then return TONE_LIT_EDGE end
	if tx == 15 or tx == 16 then return TONE_GAP end
	if kind ~= 5 and (tx == 6 or tx == 7 or tx == 24 or tx == 25) and ty > 4 and ty < 27 then return TONE_GLOW end
	if ty == 7 or ty == 22 then return TONE_LIT_EDGE end
	if ty == 8 or ty == 9 or ty == 23 or ty == 24 then return TONE_ACCENT end
	if ty >= 14 and ty <= 17 and (tx == 13 or tx == 18) then return TONE_LIT_EDGE end
	if tx % 5 == 0 then return TONE_DARK_EDGE end
	return grit(kind, tx, ty)
end

-- the exit: a switch box over hazard stripes, two green lamps
local function switch_tone(tx, ty)
	if tx < 2 or tx > 29 or ty < 2 then return TONE_DARK_EDGE end
	if ty >= 24 then
		if (((tx + ty) >> 2) & 1) == 0 then return TONE_ACCENT end
		return TONE_GAP
	end
	if tx >= 9 and tx <= 22 and ty >= 5 and ty <= 20 then
		if tx == 9 or ty == 5 then return TONE_LIT_EDGE end
		if tx == 22 or ty == 20 then return TONE_GAP end
		if tx >= 15 and tx <= 16 and ty >= 7 and ty <= 14 then return TONE_LIT_EDGE end
		if (tx == 12 or tx == 19) and ty >= 16 and ty <= 18 then return TONE_GLOW end
		return TONE_GRIT_DARK
	end
	return grit(6, tx, ty)
end

-- the showcase texture of a wall kind: tone at texel (tx, ty), both 0..31
function showcase_tone(kind, tx, ty)
	if kind == 1 then return stone_tone(tx, ty) end
	if kind == 2 then return panel_tone(tx, ty) end
	if kind == 3 then return conduit_tone(tx, ty) end
	if kind == 4 then return block_tone(tx, ty) end
	if kind == 6 then return switch_tone(tx, ty) end
	if kind >= 5 then return door_tone(kind, tx, ty) end
	return TONE_BASE
end

-- --------------------------------------------------------------- sprites ----
-- Sprites keep more light than walls at the same distance so a threat stays
-- readable down a dark corridor; uppercase palette keys (eyes, gobs, flame)
-- and every projectile glow at full brightness.
local function is_upper(key)
	local b = key:byte(1)
	return b >= 65 and b <= 90
end

function showcase_sprite_lut(name, band, flash, variant)
	local mood = MOODS[G.map_i + 1]
	local art = ARTS[name]
	local glowing = name:sub(1, 3) == "pr_"
	local lum = 1.0 - band * 0.085
	local glow = sprite_light(mood, variant)
	local light = cadd(cscale(cmix(mood.light, rgb(255, 255, 255), 0.55), lum), glow)
	local haze = cscale(mood.fog, (1.0 - lum) * 0.8 * (1.0 - 0.15 * light_level(variant)))
	local lut = {}
	local argb = art.rgb
	local keys = art.keys
	for c = 1, #argb do
		local e = argb[c]
		local r, g, b = e[1], e[2], e[3]
		if flash == 1 then
			r = imin(255, r + 170)
			g = imin(255, g + 150)
			b = imin(255, b + 140)
		end
		local col_ = rgb(r, g, b)
		if glowing or is_upper(keys[c]) then
			lut[c] = color_idx(col_)
		else
			lut[c] = color_idx(cadd(cmul(col_, light), haze))
		end
	end
	return lut
end

-- per-kind particle ramps, phase 0 = fresh; [kind + 1][phase + 1]
SHOWCASE_PARTICLE_RAMPS = {
	{rgb(0, 0, 0), rgb(0, 0, 0), rgb(0, 0, 0), rgb(0, 0, 0)},
	{rgb(255, 250, 214), rgb(255, 212, 110), rgb(240, 140, 50), rgb(150, 70, 30)},      -- spark
	{rgb(232, 40, 30), rgb(190, 30, 24), rgb(140, 22, 18), rgb(90, 14, 12)},            -- blood
	{rgb(214, 72, 54), rgb(172, 50, 38), rgb(128, 36, 28), rgb(86, 24, 20)},            -- gib
	{rgb(104, 98, 94), rgb(84, 80, 78), rgb(64, 62, 62), rgb(46, 44, 46)},             -- smoke
	{rgb(255, 255, 236), rgb(255, 240, 170), rgb(255, 200, 100), rgb(220, 130, 50)},    -- flash
	{rgb(190, 255, 200), rgb(130, 245, 220), rgb(100, 200, 255), rgb(110, 120, 240)}    -- hexmote
}

-- ----------------------------------------------------------------- light ----
-- The lit tables: one palette index per (light variant, side, shade, tone), so
-- a surface under a lamp, a muzzle flash or a hex bolt costs one lookup more
-- than an unlit one. Variant 0 is unlit; lights.lua names the rest.

SURFACE_SHADES = 16
FAR_TEXTURE = 3.2     -- past this distance surfaces draw their far texture
FLOOR_TONES = 12      -- the eight wall tones, then blood, dried blood, scorch, ash
TONE_BLOOD = 8
TONE_BLOOD_DRY = 9
TONE_SCORCH = 10
TONE_ASH = 11

-- the lit tables' colors, rounded to steps of LIT_STEP a channel
LIT_STEP = 6

function lit_idx(c)
	local half = LIT_STEP // 2
	local r = (trunc(c[1] * 255.0) + half) // LIT_STEP * LIT_STEP
	local g = (trunc(c[2] * 255.0) + half) // LIT_STEP * LIT_STEP
	local b = (trunc(c[3] * 255.0) + half) // LIT_STEP * LIT_STEP
	return rgb_idx(r, g, b)
end

function light_color_of(mood, variant)
	local channel = (variant - 1) // LIGHT_LEVELS
	if channel == LIGHT_LAMP then return cmix(mood.light, LIGHT_COLORS[LIGHT_LAMP + 1], 0.5) end
	return LIGHT_COLORS[channel + 1]
end

-- [variant][side][shade][tone] palette indices for one surface, appended to out
local function push_lit_tones(out, mood, albedo, glow, sides)
	local white = rgb(255, 255, 255)
	local black = rgb(0, 0, 0)
	local n = #out
	local ntones = #albedo
	for v = 0, LIGHT_VARIANTS - 1 do
		local s = v == 0 and 0.0 or 1.0 * light_level(v) / LIGHT_LEVELS
		local lc = v == 0 and black or light_color_of(mood, v)
		for side = 0, sides - 1 do
			local light = cscale(side == 1 and cscale(cmix(mood.light, mood.turned, 0.3), 0.7) or mood.light, AMBIENT)
			for shade = 0, SURFACE_SHADES - 1 do
				local lum = shade_light(shade)
				local haze = cscale(mood.fog, 1.0 - lum)
				local lit = cadd(cscale(light, lum), cscale(lc, s * LIGHT_GAIN * (0.45 + 0.55 * lum)))
				local fog = cscale(haze, 1.0 - 0.6 * s)
				for tone = 0, ntones - 1 do
					n = n + 1
					if tone == TONE_GLOW then
						out[n] = lit_idx(cadd(cscale(cmix(glow, white, 0.15), 0.6 + 0.4 * lum), cscale(haze, 0.3)))
					else
						out[n] = lit_idx(cadd(cmul(albedo[tone + 1], lit), fog))
					end
				end
			end
		end
	end
end

local function wall_albedo(base, accent)
	return {base, accent, cscale(base, 0.36), cclamp(cscale(base, 1.25), 0.0, 1.0), cscale(base, 0.62), base,
		cscale(base, 0.86), cclamp(cscale(base, 1.1), 0.0, 1.0)}
end

WALL_TABLE_STRIDE = 6400    -- LIGHT_VARIANTS x 2 sides x 16 shades x 8 tones

-- the lit wall table of floor mi: [kind 0..8][variant][side][shade][tone]
function showcase_lit_wall_tones(mi)
	local mood = MOODS[mi + 1]
	local tones = {}
	local unseen = rgb(10, 10, 12)
	push_lit_tones(tones, mood, wall_albedo(unseen, unseen), unseen, 2)
	for _, wall in ipairs(mood.walls) do
		push_lit_tones(tones, mood, wall_albedo(wall, cmix(wall, mood.light, 0.18)), wall, 2)
	end
	for _, fixture in ipairs(FIXTURES) do
		push_lit_tones(tones, mood, wall_albedo(fixture.base, fixture.accent), fixture.glow, 2)
	end
	return tones
end

-- what each floor is paved with, and what glows in its cracks
FLOOR_GLOWS = {rgb(255, 210, 140), rgb(90, 236, 226), rgb(255, 122, 40), rgb(255, 60, 44)}

function showcase_floor_tones(mi)
	local mood = MOODS[mi + 1]
	local base = cclamp(cscale(mood.floor, 1.3), 0.0, 1.0)
	local accent = cmix(base, mood.walls[1], 0.35)
	local albedo = wall_albedo(base, accent)
	albedo[TONE_GAP + 1] = cscale(base, 0.55)
	albedo[TONE_GRIT_DARK + 1] = cscale(base, 0.9)
	albedo[TONE_GRIT_LIGHT + 1] = cclamp(cscale(base, 1.06), 0.0, 1.0)
	albedo[9] = rgb(150, 16, 12)
	albedo[10] = rgb(84, 10, 10)
	albedo[11] = rgb(22, 18, 16)
	albedo[12] = rgb(52, 46, 44)
	local tones = {}
	push_lit_tones(tones, mood, albedo, FLOOR_GLOWS[mi + 1], 1)
	return tones
end

function showcase_ceiling_tones(mi)
	local mood = MOODS[mi + 1]
	local base = cscale(mood.ceiling, 0.85)
	local albedo = wall_albedo(base, cmix(base, mood.light, 0.12))
	albedo[TONE_GRIT_DARK + 1] = cscale(base, 0.92)
	albedo[TONE_GRIT_LIGHT + 1] = base
	local tones = {}
	push_lit_tones(tones, mood, albedo, cmix(mood.light, rgb(255, 255, 255), 0.45), 1)
	return tones
end

-- ------------------------------------------------------ floor and ceiling ----
-- 16x16 texels a tile; four variants a floor, picked per tile from a hash

-- each texture comes twice: as drawn, then as seen from afar, where the
-- one-texel lines and the grit would shimmer: only glow, tint and beams stay
local function far_tone(tone, keep_edges)
	if tone == TONE_GLOW or tone == TONE_ACCENT then return tone end
	if keep_edges and (tone == TONE_GAP or tone == TONE_DARK_EDGE) then return tone end
	return TONE_BASE
end

local function floor_tone(mi, variant, u, v)
	local n = texel_noise(20 + variant, u, v)
	if mi == 1 then
		-- coolant grating: plates over a lit channel
		if u == 0 or v == 0 then return TONE_LIT_EDGE end
		if u == 15 or v == 15 then return TONE_GAP end
		if v >= 6 and v <= 9 and u >= 2 and u <= 13 and (u & 1) == 0 then
			if variant == 0 then return TONE_GLOW end
			return TONE_GAP
		end
		if (u == 3 or u == 12) and (v == 3 or v == 12) then return TONE_LIT_EDGE end
		if n < 30 then return TONE_GRIT_DARK end
		return TONE_BASE
	end
	if mi == 2 then
		-- foundry basalt: blocks split by cracks that glow
		local x = (u + (v >> 3) * 5) & 15
		local y = v & 7
		local crack_at = 99
		if variant == 0 then crack_at = (v * 3) // 4 + 3 + ((v >> 2) & 1) end
		if u == crack_at or (variant == 0 and u == crack_at + 1 and (v & 3) == 0) then return TONE_GLOW end
		if x == 15 or y == 7 then return TONE_GAP end
		if x == 0 or y == 0 then return TONE_LIT_EDGE end
		if n < 40 then return TONE_GRIT_DARK end
		if variant == 3 then return TONE_ACCENT end
		return TONE_BASE
	end
	if mi == 3 then
		-- the throne: checkered tiles trimmed in gold
		if u == 0 or v == 0 or u == 8 or v == 8 then return TONE_LIT_EDGE end
		local dark = ((u >> 3) + (v >> 3)) % 2 == 0
		if dark then
			if n < 50 then return TONE_GRIT_DARK end
			return TONE_DARK_EDGE
		end
		if n < 30 then return TONE_GRIT_LIGHT end
		return TONE_ACCENT
	end
	-- flagstones, four to a tile, each its own tint
	local sx = u >> 3
	local sy = v >> 3
	local x = u & 7
	local y = v & 7
	if x == 7 or y == 7 then return TONE_GAP end
	if x == 0 or y == 0 then return TONE_LIT_EDGE end
	if x == 6 or y == 6 then return TONE_DARK_EDGE end
	if (sx + sy * 2 + variant) % 3 == 0 then
		if n < 24 then return TONE_GRIT_DARK end
		return TONE_ACCENT
	end
	if n < 26 then return TONE_GRIT_DARK end
	if n > 238 then return TONE_GRIT_LIGHT end
	return TONE_BASE
end

-- [far][variant][v][u], flat
function showcase_floor_texture(mi)
	local tex = {}
	local n = 0
	for far = 0, 1 do
		for variant = 0, 3 do
			for v = 0, 15 do
				for u = 0, 15 do
					local tone = floor_tone(mi, variant, u, v)
					if far == 1 then
						if mi == 1 then tone = far_tone(tone, false)
						elseif tone == TONE_GLOW then tone = TONE_DARK_EDGE
						else tone = far_tone(tone, false) end
					end
					n = n + 1
					tex[n] = tone
				end
			end
		end
	end
	return tex
end

local function ceiling_tone(lamp, u, v)
	if u < 2 or v < 2 then
		if u == 1 or v == 1 then return TONE_GAP end
		return TONE_DARK_EDGE
	end
	if lamp and u >= 5 and u <= 12 and v >= 5 and v <= 12 then
		if u == 5 or v == 5 or u == 12 or v == 12 then return TONE_LIT_EDGE end
		return TONE_GLOW
	end
	if u == 2 or v == 2 then return TONE_LIT_EDGE end
	return grit(9, u, v)
end

-- ceiling panels between beams; variant 1 is a lamp: [far][lamp][v][u]
function showcase_ceiling_texture()
	local tex = {}
	local n = 0
	for far = 0, 1 do
		for lamp = 0, 1 do
			for v = 0, 15 do
				for u = 0, 15 do
					local tone = ceiling_tone(lamp == 1, u, v)
					n = n + 1
					if far == 1 then tex[n] = far_tone(tone, true) else tex[n] = tone end
				end
			end
		end
	end
	return tex
end

-- every wall kind's texture as tones, 32x32 texels: [far][kind][ty][tx]
function showcase_wall_texture()
	local tex = {}
	local n = 0
	for far = 0, 1 do
		for kind = 0, 8 do
			for ty = 0, 31 do
				for tx = 0, 31 do
					local tone = showcase_tone(kind, tx, ty)
					n = n + 1
					if far == 1 then tex[n] = far_tone(tone, true) else tex[n] = tone end
				end
			end
		end
	end
	return tex
end

-- a sprite's light: how a variant tints it
function sprite_light(mood, variant)
	if variant == 0 then return rgb(0, 0, 0) end
	return cscale(light_color_of(mood, variant), 0.9 * light_level(variant) / LIGHT_LEVELS)
end
