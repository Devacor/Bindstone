-- The showcase look's feel: what the game does on screen when something
-- happens. Feel watches the game from outside, frame by frame: it compares what
-- it sees with what it saw (a shot spent, an enemy flinching, ammo gone, a door
-- open) and answers with light sources, screen shake, view bob, decals on the
-- floor, hit markers, screen tints, and the iris and title card that open a
-- floor. It reads the simulation and never writes to it, so the reference
-- frames and STATE_HASH never notice it. (The reference's sound cues are left
-- out: the ports run muted.)

local sqrt, cos, sin, abs = math.sqrt, math.cos, math.sin, math.abs

Feel = {}
Feel.__index = Feel

function Feel.new()
	return setmetatable({
		yaw = 0.0,         -- shake: a turn of the view, radians
		pitch = 0,         -- bob and shake: pixel rows the horizon moves
		sources = {},      -- this frame's lights over the bake

		last_tick = 0,
		last_map_ticks = 0,
		last_hp = 100,
		last_armor = 0,
		last_ammo = {48, 0, 0},
		last_key_r = false,
		last_key_b = false,
		last_secrets = 0,
		last_bob = 0.0,
		bob_amp = 0.0,
		e_flash = {},
		e_alive = {},
		shot_kind = {},
		shot_x = {},
		shot_y = {},
		blasts = {},       -- a light that outlives the shot that made it: {x, y, channel, age}
		trauma = 0.0,
		hurt_t = 0,
		pickup_t = 0,
		secret_t = 0,
		blast_t = 0,
		hit_t = 0,
		hit_kill = false,
		mask_w = 0,
		mask_h = 0,
		mask_px = {},      -- vignette: the pixels it darkens, and how much (1..4)
		mask_level = {},
		mask_all = {},     -- every pixel's level, for the full-screen tints
		remaps = {},       -- [tint * 5 + level + 1]: palette index -> tinted index, -1 not yet made
		card_shades = {},
	}, Feel)
end

-- once per frame, before the view draws
function Feel:frame()
	local g = G
	local ticks = g.tick - self.last_tick
	self.last_tick = g.tick
	if ticks < 0 then ticks = 0 end
	if g.mode ~= 1 then return end
	if g.map_ticks < self.last_map_ticks or #g.enemies ~= #self.e_alive then self:new_floor() end
	self.last_map_ticks = g.map_ticks
	for i = 1, ticks do self:age_one_tick() end
	self:watch_player(ticks)
	self:watch_enemies()
	self:watch_shots()
	self:watch_world()
	self:stain_landed()
	dry_stains(g.tick)
	self:gather_lights()
	self:camera()
end

-- after the view, the gun and the particles: tints, the vignette, the hit marker, the floor's opening
function Feel:overlay(vw, vh)
	if self.mask_w ~= vw or self.mask_h ~= vh then self:build_mask(vw, vh) end
	local base = 0
	local tint = -1
	if self.blast_t > 0 then tint = 4; base = imin(3, self.blast_t)
	elseif self.hurt_t > 0 then tint = 1; base = imin(3, self.hurt_t // 2 + 1)
	elseif self.pickup_t > 0 then tint = 2; base = imin(2, self.pickup_t // 2)
	elseif self.secret_t > 0 then tint = 3; base = imin(2, self.secret_t // 3)
	elseif G.hp <= 25 and (G.tick % 24) < 5 then tint = 1; base = 1 end
	self:shade_pass(0, 0)
	if tint >= 0 then self:shade_pass(tint, base) end
	self:draw_hit_marker(vw)
	self:opening(vw, vh)
end

function Feel:kick(amount) self.trauma = fclamp(self.trauma + amount, 0.0, 1.0) end

-- --------------------------------------------------------------- watching
function Feel:new_floor()
	local g = G
	local e_flash = {}
	local e_alive = {}
	local enemies = g.enemies
	for i = 1, #enemies do
		e_flash[i] = enemies[i].flash
		e_alive[i] = enemies[i].alive
	end
	self.e_flash = e_flash
	self.e_alive = e_alive
	self.shot_kind = {}
	self.shot_x = {}
	self.shot_y = {}
	self.blasts = {}
	self.last_secrets = g.secrets
	self.last_hp = g.hp
	self.last_armor = g.armor
	self.last_ammo = {g.ammo[1], g.ammo[2], g.ammo[3]}
	self.card_shades = {}
	self.trauma = 0.0
	self.hurt_t = 0
	self.pickup_t = 0
	self.secret_t = 0
	self.blast_t = 0
	self.hit_t = 0
end

function Feel:age_one_tick()
	local trauma = self.trauma * 0.86
	if trauma < 0.01 then trauma = 0.0 end
	self.trauma = trauma
	if self.hurt_t > 0 then self.hurt_t = self.hurt_t - 1 end
	if self.pickup_t > 0 then self.pickup_t = self.pickup_t - 1 end
	if self.secret_t > 0 then self.secret_t = self.secret_t - 1 end
	if self.blast_t > 0 then self.blast_t = self.blast_t - 1 end
	if self.hit_t > 0 then self.hit_t = self.hit_t - 1 end
	local kept = {}
	local blasts = self.blasts
	for i = 1, #blasts do
		local b = blasts[i]
		b[4] = b[4] + 1
		if b[4] < 14 then kept[#kept + 1] = b end
	end
	self.blasts = kept
end

-- everything that fades does so per tick, so the feel is the same at any frame rate
function Feel:watch_player(ticks)
	local g = G
	local hp, armor, ammo = g.hp, g.armor, g.ammo
	local last_ammo = self.last_ammo
	local lost = (self.last_hp - hp) + (self.last_armor - armor)
	if hp < self.last_hp then
		self.hurt_t = 8
		self:kick(0.25 + lost / 30.0)
	end
	for pool = 1, 3 do
		if ammo[pool] < last_ammo[pool] then
			local weapon = g.weapon
			if weapon == 0 then self:kick(0.10)
			elseif weapon == 1 then self:kick(0.42)
			else self:kick(0.18) end
		elseif ammo[pool] > last_ammo[pool] then
			self.pickup_t = 5
		end
	end
	if (g.key_r and not self.last_key_r) or (g.key_b and not self.last_key_b) then self.pickup_t = 6 end
	if hp > self.last_hp or armor > self.last_armor then self.pickup_t = 5 end
	self.last_hp = hp
	self.last_armor = armor
	self.last_ammo = {ammo[1], ammo[2], ammo[3]}
	self.last_key_r = g.key_r
	self.last_key_b = g.key_b
	local moved = g.bob - self.last_bob
	self.last_bob = g.bob
	for i = 1, ticks do
		if moved > 0.0 then self.bob_amp = fclamp(self.bob_amp + 0.25, 0.0, 1.0)
		else self.bob_amp = self.bob_amp * 0.8 end
	end
end

function Feel:watch_enemies()
	local enemies = G.enemies
	local e_flash = self.e_flash
	local e_alive = self.e_alive
	for i = 1, #enemies do
		local e = enemies[i]
		if e.flash > e_flash[i] and e_alive[i] then
			self.hit_t = 6
			self.hit_kill = false
		end
		if e_alive[i] and not e.alive then
			self.hit_t = 9
			self.hit_kill = true
			self:kick(e.kind == 3 and 0.6 or 0.12)
			stain_floor(e.x, e.y, e.kind == 2 and 0.0 or 0.34, TONE_BLOOD, 170)
		end
		e_flash[i] = e.flash
		e_alive[i] = e.alive
	end
end

-- a shot gone since the last frame burst somewhere near where it was
function Feel:watch_shots()
	local g = G
	local shots = g.shots
	local matched = {}
	for j = 1, #shots do matched[j] = false end
	local shot_kind, shot_x, shot_y = self.shot_kind, self.shot_x, self.shot_y
	for i = 1, #shot_kind do
		local found = false
		for j = 1, #shots do
			local s = shots[j]
			if not matched[j] and s[1] == shot_kind[i] and dist2(s[2], s[3], shot_x[i], shot_y[i]) < 1.2 then
				matched[j] = true
				found = true
				break
			end
		end
		local kind = shot_kind[i]
		if not found and kind ~= 1 then
			self.blasts[#self.blasts + 1] = {shot_x[i], shot_y[i], kind == 3 and LIGHT_HEX or LIGHT_FIRE, 0}
			stain_floor(shot_x[i], shot_y[i], 0.55, TONE_SCORCH, 190)
			stain_floor(shot_x[i], shot_y[i], 0.8, TONE_ASH, 40)
			local d = sqrt(dist2(shot_x[i], shot_y[i], g.px, g.py))
			if d < 6.0 then
				self:kick(0.75 * (1.0 - d / 6.0) + 0.1)
				self.blast_t = imax(self.blast_t, d < 3.0 and 3 or 2)
			end
		end
	end
	shot_kind = {}
	shot_x = {}
	shot_y = {}
	for j = 1, #shots do
		local s = shots[j]
		shot_kind[j] = s[1]
		shot_x[j] = s[2]
		shot_y[j] = s[3]
	end
	self.shot_kind, self.shot_x, self.shot_y = shot_kind, shot_x, shot_y
end

function Feel:watch_world()
	local g = G
	if g.secrets > self.last_secrets then self.secret_t = 9 end
	self.last_secrets = g.secrets
end

-- blood and meat that reached the floor leave their mark
function Feel:stain_landed()
	local pool = G.fx.pool
	for i = 1, PART_MAX do
		local p = pool[i]
		local kind = p[1]
		if (kind == 2 or kind == 3) and p[4] <= 0.021 then
			stain_floor(p[2], p[3], kind == 3 and 0.09 or 0.06, TONE_BLOOD, 230)
		end
	end
end

-- ----------------------------------------------------------------- light
local SHOT_LIGHT = {LIGHT_ACID, LIGHT_FIRE, LIGHT_HEX}   -- [shot kind]

function Feel:gather_lights()
	local g = G
	local sources = {}
	local n = 0
	if g.muzzle > 0 then
		local dx = cos(g.pang)
		local dy = sin(g.pang)
		local channel = g.weapon == 2 and LIGHT_HEX or LIGHT_FIRE
		n = n + 1
		sources[n] = light_source(g.px + dx * 0.6, g.py + dy * 0.6, channel, MUZZLE_RADIUS * (0.55 + 0.15 * g.muzzle), 1.0)
	end
	local shots = g.shots
	for i = 1, #shots do
		local s = shots[i]
		n = n + 1
		sources[n] = light_source(s[2], s[3], SHOT_LIGHT[s[1]], SHOT_RADIUS, 0.9)
	end
	local blasts = self.blasts
	for i = 1, #blasts do
		local b = blasts[i]
		local t = 1.0 - b[4] / 14.0
		n = n + 1
		sources[n] = light_source(b[1], b[2], b[3], BLAST_RADIUS * (0.4 + 0.6 * t), 0.35 + 0.65 * t)
	end
	self.sources = sources
end

-- ---------------------------------------------------------------- camera
-- shake is trauma squared, so small knocks barely move the view and big ones rattle it
function Feel:camera()
	local g = G
	local shake = self.trauma * self.trauma * SHAKE
	local n1 = (mix32(g.tick, 101) % 2001) / 1000.0 - 1.0
	local n2 = (mix32(g.tick, 202) % 2001) / 1000.0 - 1.0
	self.yaw = shake * 0.045 * n1
	local bob = (abs(cos(g.bob * 0.5)) - 0.5) * 2.0 * BOB_PIXELS * self.bob_amp * PIXH / 2.0
	self.pitch = trunc(bob + shake * 5.0 * PIXH / 2.0 * n2)
end

-- ------------------------------------------------------------- overlays
function Feel:build_mask(vw, vh)
	self.mask_w = vw
	self.mask_h = vh
	local mask_px = {}
	local mask_level = {}
	local mask_all = {}
	local n = 0
	local cx = vw / 2.0
	local cy = vh / 2.0
	for y = 0, vh - 1 do
		for x = 0, vw - 1 do
			local dx = (x + 0.5 - cx) / cx
			local dy = (y + 0.5 - cy) / cy
			local e = dx * dx * 0.8 + dy * dy
			local level = iclamp(trunc((e - 0.5) * 3.2 + BAYER4[(y & 3) * 4 + (x & 3) + 1]), 0, 4)
			mask_all[y * vw + x + 1] = level
			if level > 0 then
				n = n + 1
				mask_px[n] = y * vw + x + 1
				mask_level[n] = level
			end
		end
	end
	self.mask_px = mask_px
	self.mask_level = mask_level
	self.mask_all = mask_all
end

-- tint 0 darkens the corners; the others wash the screen from the edges in: 1 hurt, 2 pickup, 3 secret, 4 blast
function Feel:shade_pass(tint, base)
	local remaps = self.remaps
	while #remaps < 25 do remaps[#remaps + 1] = {} end
	local pal = #PAL_RGB
	for level = 1, 4 do
		local tab = remaps[tint * 5 + level + 1]
		for p = #tab + 1, pal do tab[p] = -1 end
	end
	local pix = G.pix
	if tint == 0 then
		if VIGNETTE <= 0.0 then return end
		local mask_px = self.mask_px
		local mask_level = self.mask_level
		for k = 1, #mask_px do
			local i = mask_px[k]
			pix[i] = self:remapped(0, mask_level[k], pix[i])
		end
		return
	end
	local mask_all = self.mask_all
	for i = 1, G.vw * G.vh do
		local level = imin(4, mask_all[i] + base)
		if level > 0 then pix[i] = self:remapped(tint, level, pix[i]) end
	end
end

local TOWARD = {{0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}, {255.0, 222.0, 120.0}, {120.0, 255.0, 230.0}, {255.0, 236.0, 200.0}}

function Feel:remapped(tint, level, p)
	local tab = self.remaps[tint * 5 + level + 1]
	local m = tab[p]
	if m >= 0 then return m end
	local key = PAL_RGB[p]
	local r = 1.0 * ((key >> 16) & 255)
	local g = 1.0 * ((key >> 8) & 255)
	local b = 1.0 * (key & 255)
	local a = level / 4.0
	if tint == 0 then
		local f = 1.0 - VIGNETTE * a
		m = rgb_idx(trunc(r * f), trunc(g * f), trunc(b * f))
	elseif tint == 1 then
		m = rgb_idx(trunc(r + (255.0 - r) * a * 0.55), trunc(g * (1.0 - a * 0.65)), trunc(b * (1.0 - a * 0.65)))
	else
		local toward = TOWARD[tint + 1]
		local k = tint == 4 and a * 0.55 or a * 0.32
		m = rgb_idx(trunc(r + (toward[1] - r) * k), trunc(g + (toward[2] - g) * k), trunc(b + (toward[3] - b) * k))
	end
	tab[p] = m
	return m
end

-- four ticks around the aim point: white on a hit, red and wider on a kill
function Feel:draw_hit_marker(vw)
	local g = G
	if self.hit_t <= 0 or g.automap then return end
	local color = self.hit_kill and rgb_idx(255, 64, 48) or rgb_idx(255, 250, 236)
	local cx = vw // 2
	local cy = g.horizon
	local gap = (self.hit_kill and 3 or 2) * PIXW
	local len = 2 * PIXW
	local pix = g.pix
	local vh = g.vh
	for k = 0, len - 1 do
		local ox = gap + k
		local oy = (gap + k) * PIXH // PIXW // 2 + 1
		for s = 0, 3 do
			local x = cx + ((s & 1) == 0 and ox or -ox)
			local y = cy + ((s & 2) == 0 and oy or -oy)
			if x >= 0 and x < vw and y >= 0 and y < vh then pix[y * vw + x + 1] = color end
		end
	end
end

-- a floor opens with an iris from the center, then its name, which fades into the dark
function Feel:opening(vw, vh)
	local g = G
	local t = g.map_ticks
	if t >= 80 then return end
	local pix = g.pix
	if t < 16 then
		-- in cell widths: a pixel is 1 / PIXW of a cell across and 2 / PIXH down
		local cx = vw / 2.0
		local cy = vh / 2.0
		local sx = 1.0 / PIXW
		local sy = 2.0 / PIXH
		local r = (t / 16.0) * sqrt(cx * sx * cx * sx + cy * sy * cy * sy) * 1.02
		local r2 = r * r
		local black = PAL_BLACK
		for y = 0, vh - 1 do
			local dy = (y + 0.5 - cy) * sy
			local row = y * vw + 1
			for x = 0, vw - 1 do
				local dx = (x + 0.5 - cx) * sx
				if dx * dx + dy * dy > r2 then pix[row + x] = black end
			end
		end
	end
	if t < 6 or g.automap then return end
	if #self.card_shades == 0 then self:build_card_shades() end
	local fade = t < 60 and 0 or (t - 60) // 4
	if fade >= 5 then return end
	local name = g.map_name
	local code = name:sub(1, 4)
	local title = name:sub(7)
	-- a font pixel spans sx view pixels across and sy down, so letters stay square on 1:2 cells
	local sx = imax(1, PIXW * 2 // PIXH)
	local scale = 3
	while scale > 1 and lettering_width(title, scale) * sx > vw - 8 do scale = scale - 1 end
	local top = vh // 5
	local shades = self.card_shades
	self:pix_lettering(code, idivt(vw - lettering_width(code, 1) * sx, 2), top, 1, sx, shades[fade + 6], shades[11])
	self:pix_lettering(title, idivt(vw - lettering_width(title, scale) * sx, 2), top + 8, scale, sx, shades[fade + 1],
		shades[11])
end

function Feel:build_card_shades()
	local mood = MOODS[G.map_i + 1]
	local bone = rgb(250, 240, 220)
	local shades = {}
	for i = 0, 4 do shades[i + 1] = color_idx(cmix(bone, mood.fog, i / 5.0)) end
	for i = 0, 4 do shades[i + 6] = color_idx(cmix(mood.light, mood.fog, i / 5.0)) end
	shades[11] = rgb_idx(4, 2, 4)
	self.card_shades = shades
end

-- the 3x5 font straight into the view's pixels, a font pixel scale rows tall and scale * sx columns wide
function Feel:pix_lettering(s, x, y, scale, sx, color, shadow)
	local g = G
	local pix = g.pix
	local vw = g.vw
	local vh = g.vh
	local sy = scale
	local cw = sx * scale
	for pass = 0, 1 do
		local ink = pass == 0 and shadow or color
		local off = pass == 0 and 1 or 0
		for i = 0, #s - 1 do
			local glyph_rows = FONT5[s:sub(i + 1, i + 1)]
			if glyph_rows then
				local gx = x + i * 4 * cw
				for ry = 0, 4 do
					local grow = glyph_rows[ry + 1]
					for rx = 0, 2 do
						if grow:sub(rx + 1, rx + 1) == "#" then
							for yy = 0, sy - 1 do
								local py = y + (ry + off) * sy + yy
								if py >= 0 and py < vh then
									for xx = 0, cw - 1 do
										local qx = gx + (rx + off) * cw + xx
										if qx >= 0 and qx < vw then pix[py * vw + qx + 1] = ink end
									end
								end
							end
						end
					end
				end
			end
		end
	end
end
