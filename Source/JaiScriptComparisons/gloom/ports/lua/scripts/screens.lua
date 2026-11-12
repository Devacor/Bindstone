-- The showcase look's status bar, panel and full-screen pages (title, floor
-- tally, death, victory), painted on half-block canvases. hud.lua keeps the
-- reference look's versions, the frames --smoke hashes.

local concat = table.concat
local format = string.format

-- the face in the panel: 10x10 pixel art, one per expression (face_name)
FACE_PAL = {h = {58, 40, 30}, s = {214, 160, 120}, S = {156, 106, 78}, e = {240, 236, 226}, p = {34, 30, 40}, B = {70, 44, 30}, m = {96, 30, 30}, t = {236, 228, 210}, b = {186, 26, 22}, g = {150, 150, 140}, x = {40, 20, 20}}
FACE_SRC = {
	ok = {"..hhhhhh..", ".hhhhhhhh.", ".hsssssshh", ".sBBssBBsh", ".sepssepsS", ".sssSsssss", ".ssSSSssS.", ".ssmmmmsS.", "..ssssss..", "...SSSS..."},
	hurt = {"..hhhhhh..", ".hhhhhhhh.", ".hsssssshh", ".sBBssBBsh", ".sepssepsS", ".sssSssSSs", ".ssSSSsSS.", ".ssmmmmsS.", "..sSssss..", "...SSSS..."},
	bad = {"..hhhhhh..", ".hhbhhhhh.", ".hsbsssshh", ".sBBssBBsh", ".sxxsseps.", ".bssSssSSs", ".bsSSSsSS.", ".ssmmmmsS.", "..sSsSss..", "...SSSS..."},
	crit = {"..hhbhhh..", ".hhbbhhhh.", ".hbbsbsshh", ".sBBbsBBsh", ".sxxssxxb.", ".bbsSssSbs", ".bsSSSsbS.", ".sbttttbS.", "..sbsSsb..", "...SbSS..."},
	pain = {"..hhhhhh..", ".hhhhhhhh.", ".hsBBsBBhh", ".sssssssh.", ".sxxssxxs.", ".sssSsssss", ".ssmmmmsS.", ".ssmttmsS.", "..smmmms..", "...SSSS..."},
	grin = {"..hhhhhh..", ".hhhhhhhh.", ".hsssssshh", ".sBBssBBsh", ".sepssepsS", ".sssSsssss", ".smSSSSms.", ".sttttttS.", "..smmmms..", "...SSSS..."},
	dead = {"..hhhhhh..", ".hhhbhhhh.", ".hgggggghh", ".gxgggxgg.", ".ggxggxgg.", ".gxgggxgg.", ".gggSgggg.", ".ggxxxxgb.", "..gggggb..", "...gggb..."},
}

-- the logo, 57 x 11 pixels: G L O O M
LOGO_ART = {
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
}

Screens = {}
Screens.__index = Screens

-- a code -> palette index table for an art, unshaded
local function plain_lut(a)
	local lut = {}
	local argb = a.rgb
	for c = 1, #argb do
		local e = argb[c]
		lut[c] = rgb_idx(e[1], e[2], e[3])
	end
	return lut
end

function Screens.new(w, h)
	local self = setmetatable({
		panel = Canvas.new(w, 6),   -- the six rows under the view
		page = Canvas.new(w, h),    -- a whole-screen page
		faces = {},
		face_luts = {},
		heat = {},
		heat_n = 0,
		fire_colors = {},
		fire_tick = -1,
	}, Screens)
	for name, rows in pairs(FACE_SRC) do
		local art = parse_art({pal = FACE_PAL, rows = rows})
		self.faces[name] = art
		self.face_luts[name] = plain_lut(art)
	end
	return self
end

function Screens:frame(fps, ms_sim, ms_draw)
	local g = G
	g.feel:frame()
	local mode = g.mode
	if mode == 0 then return concat(self:title_page(), "\n") end
	if mode == 2 then return concat(self:tally_page(), "\n") end
	if mode == 3 then return concat(self:dead_page(), "\n") end
	if mode == 4 then return concat(self:victory_page(), "\n") end
	local rows = render_view()
	local n = #rows
	local hud = self:hud_rows(fps, ms_sim, ms_draw)
	for i = 1, #hud do rows[n + i] = hud[i] end
	return concat(rows, "\n")
end

-- five font-row colors from top to bottom
local function shades5(top, bottom)
	local out = {}
	for i = 0, 4 do out[i + 1] = color_idx(cmix(top, bottom, i / 4.0)) end
	return out
end

-- the status-bar color of a message (show_msg takes an SGR color code)
local function message_color(code)
	if code == "91" then return rgb_idx(255, 96, 80) end
	if code == "92" then return rgb_idx(120, 236, 120) end
	if code == "93" then return rgb_idx(255, 214, 90) end
	if code == "94" then return rgb_idx(110, 160, 255) end
	if code == "96" then return rgb_idx(110, 230, 230) end
	if code == "90" then return rgb_idx(130, 126, 136) end
	return rgb_idx(240, 236, 228)
end

-- a labelled big number with a bar beneath it
local function stat_block(c, x, width, label, value, bar, bar_max, digits, bar_color)
	c:text(x + 1, 1, label, rgb_idx(150, 140, 132))
	local number = tostring(value)
	c:lettering(number, x + width - 1 - lettering_width(number, 1), 4, 1, digits, rgb_idx(16, 12, 14))
	local bar_w = width - 2
	local filled = iclamp(idivt(bar * bar_w, imax(1, bar_max)), 0, bar_w)
	c:rect(x + 1, 10, bar_w, 1, rgb_idx(40, 36, 44))
	c:rect(x + 1, 10, filled, 1, bar_color)
end

local function key_chip(c, x, row, name, held, color)
	if held then
		c:rect(x, row * 2, #name + 2, 2, color)
		c:text(x + 1, row, name, rgb_idx(255, 255, 255))
	else
		c:text(x + 1, row, name, rgb_idx(76, 70, 82))
	end
end

-- ---------------------------------------------------------------- HUD ----
-- HUD rows under the view: the status bar, then health, armor, the face, the
-- weapon's ammo and the arms rack with keycards
function Screens:hud_rows(fps, ms_sim, ms_draw)
	local g = G
	local c = self.panel
	local w = c.w
	local mood = MOODS[g.map_i + 1]
	c:clear(rgb_idx(14, 12, 18))
	c:gradient(2, 12, rgb(62, 58, 70), rgb(22, 20, 26))
	c:rect(0, 11, w, 1, rgb_idx(10, 8, 12))

	-- status bar: where, how it is going, what just happened
	local dim = rgb_idx(132, 124, 136)
	local map_code = g.map_name:sub(1, 4)
	local map_title = g.map_name:sub(7)
	c:text(1, 0, map_code, color_idx(mood.light))
	c:text(6, 0, map_title, rgb_idx(214, 206, 196))
	local tally = "KILLS " .. g.kills .. "/" .. g.kill_total .. "   SECRETS " .. g.secrets .. "/" .. g.secret_total
	local tally_x = 7 + #map_title + 3
	c:text(tally_x, 0, tally, dim)
	if g.msg_t > 0 then
		local room = w - (tally_x + #tally) - 3
		local m = g.msg
		if #m > room then m = m:sub(1, imax(0, room)) end
		c:text_right(w - 1, 0, m, message_color(g.msg_color))
	end

	local stat_w = iclamp(idivt(w - 33, 3), 12, 17)
	local x_hp = 1
	local x_armor = x_hp + stat_w + 1
	local x_face = x_armor + stat_w + 1
	local x_ammo = x_face + 13
	local x_arms = x_ammo + stat_w + 1
	local sep = rgb_idx(14, 12, 18)
	for _, x in ipairs({x_armor - 1, x_face - 1, x_ammo - 1, x_arms - 1}) do c:rect(x, 3, 1, 8, sep) end

	local red_digits = shades5(rgb(255, 220, 130), rgb(214, 64, 40))
	local blue_digits = shades5(rgb(200, 232, 255), rgb(64, 120, 220))
	local gold_digits = shades5(rgb(255, 244, 196), rgb(214, 150, 54))
	local hp = g.hp
	local hp_bar
	if hp > 100 then hp_bar = rgb_idx(120, 220, 255)
	elseif hp > 60 then hp_bar = rgb_idx(110, 220, 90)
	elseif hp > 30 then hp_bar = rgb_idx(240, 190, 60)
	else hp_bar = rgb_idx(240, 60, 40) end
	stat_block(c, x_hp, stat_w, "HEALTH", hp, imin(hp, 100), 100, red_digits, hp_bar)
	stat_block(c, x_armor, stat_w, "ARMOR", g.armor, g.armor, 100, blue_digits, rgb_idx(90, 150, 240))

	c:rect(x_face, 2, 12, 10, rgb_idx(18, 16, 22))
	local face = face_name()
	c:art(self.faces[face], x_face + 1, 2, self.face_luts[face])

	local weapon = WEAPONS[g.weapon + 1]
	local pool = weapon.ammo
	local pool_colors = {rgb_idx(226, 186, 84), rgb_idx(226, 96, 64), rgb_idx(90, 230, 220)}
	local ammo = g.ammo
	stat_block(c, x_ammo, stat_w, weapon.name, ammo[pool + 1], ammo[pool + 1], AMMO_MAX[pool + 1], gold_digits,
		pool_colors[pool + 1])

	local arms_w = w - x_arms - 1
	if arms_w >= 11 then
		c:text(x_arms + 1, 1, "ARMS", dim)
		for slot = 0, 2 do
			local sx = x_arms + 1 + slot * 4
			local owned = g.have[slot + 1] == 1
			if slot == g.weapon then c:rect(sx, 4, 3, 2, rgb_idx(132, 100, 40)) end
			c:text(sx + 1, 2, tostring(slot + 1), owned and rgb_idx(250, 240, 210) or rgb_idx(70, 66, 76))
		end
		if arms_w >= 26 then
			c:text(x_arms + 14, 2, "B" .. ammo[1] .. " S" .. ammo[2] .. " C" .. ammo[3], dim)
		end
		key_chip(c, x_arms + 1, 3, "RED", g.key_r, rgb_idx(200, 40, 34))
		key_chip(c, x_arms + 7, 3, "BLUE", g.key_b, rgb_idx(44, 90, 210))
		if g.autopilot then
			c:text(x_arms + 1, 5, "AUTOPILOT", rgb_idx(110, 230, 230))
		elseif fps > 0.0 then
			local cores = HOST_WORKERS > 0 and ("  " .. HOST_WORKERS .. " cores") or ""
			c:text(x_arms + 1, 5, format("%.0f fps %.1f ms", fps, ms_sim + ms_draw) .. cores, dim)
		end
	end
	return c:rows(0, 6)
end

-- -------------------------------------------------------------- pages ----
local function ember(i, salt) return mix32(mix32(i * 2654435 + 7, salt), 99991) end

-- the title's fire: each cell takes the average of the cells under it, a little
-- cooler, over a bottom row of flickering embers; one step a tick, the flicker a
-- hash of the tick, so every run burns alike
function Screens:draw_fire(c, rows)
	local g = G
	local w = c.w
	local height = c.h * 2
	local fire_colors = self.fire_colors
	if #fire_colors == 0 then
		local stops = {rgb(0, 0, 0), rgb(40, 6, 6), rgb(120, 16, 8), rgb(196, 48, 10), rgb(236, 110, 20),
			rgb(250, 176, 50), rgb(255, 228, 120), rgb(255, 250, 220)}
		for i = 0, 35 do
			local t = i / 35.0 * (#stops - 1)
			local k = trunc(t)
			local k2 = imin(#stops - 1, k + 1)
			fire_colors[i + 1] = color_idx(cmix(stops[k + 1], stops[k2 + 1], t - k))
		end
	end
	if self.heat_n ~= w * (rows + 2) then
		local heat = {}
		for i = 1, w * (rows + 2) do heat[i] = 0 end
		self.heat = heat
		self.heat_n = w * (rows + 2)
		self.fire_tick = -1
	end
	local fire_tick = self.fire_tick
	local steps = fire_tick < 0 and rows * 2 or imin(4, g.tick - fire_tick)
	for s = 0, steps - 1 do self:fire_step(w, rows + 2, (fire_tick < 0 and 0 or fire_tick) + s) end
	self.fire_tick = g.tick
	local heat = self.heat
	local px = c.px
	local top = height - rows
	for y = 0, rows - 1 do
		for x = 0, w - 1 do
			local h = heat[y * w + x + 1] * 35 // 255
			if h > 2 then px[(top + y) * w + x + 1] = fire_colors[h + 1] end
		end
	end
end

function Screens:fire_step(w, rows, tick)
	local heat = self.heat
	for r = rows - 2, rows - 1 do
		for x = 0, w - 1 do
			local n = (mix32(mix32(tick, x), 17 + r) >> 9) & 255
			heat[r * w + x + 1] = n < 90 and 60 or 255
		end
	end
	local cool = 2 + 230 // imax(1, rows)
	for y = 0, rows - 3 do
		local row = y * w
		for x = 0, w - 1 do
			local below = row + w + x + 1
			local left = x > 0 and heat[below - 1] or heat[below]
			local right = x < w - 1 and heat[below + 1] or heat[below]
			local h = (left + heat[below] + right + heat[below + w]) // 4 - cool
			heat[row + x + 1] = h < 0 and 0 or h
		end
	end
end

-- sparks rising from the bottom of the page
local function draw_embers(c, count, hot, cool)
	local tick = G.tick
	local height = c.h * 2
	local period = height + 16
	for i = 0, count - 1 do
		local speed = 1 + ember(i, 1) % 3
		local x = ember(i, 2) % c.w
		local rise = (tick * speed // 2 + ember(i, 3) % period) % period
		local y = height - rise
		if (ember(i, tick // 4) & 3) ~= 0 then
			local t = 1.0 * rise / period
			c:dot(x + ((rise // 6 + i) % 3) - 1, y, color_idx(cmix(hot, cool, t)))
		end
	end
end

-- a heading in the pixel font, centered at pixel row y, at the largest scale that fits
local function heading(c, s, y, max_scale, top_bottom, shadow)
	local scale = max_scale
	while scale > 1 and lettering_width(s, scale) > c.w - 4 do scale = scale - 1 end
	c:lettering(s, idivt(c.w - lettering_width(s, scale), 2), y, scale, top_bottom, shadow)
	return 5 * scale
end

local function blink(period) return (G.tick // period) % 2 == 0 end

-- outlined, shadowed, lit from above; a bleeding logo gathers drops under its
-- letters that fall
local function draw_logo(c, x, y, top, bottom, bleeding)
	local lw = #LOGO_ART[1]
	local lh = #LOGO_ART
	local outline = rgb_idx(6, 2, 4)
	local shadow = rgb_idx(40, 6, 8)
	for ly = 0, lh - 1 do
		local row = LOGO_ART[ly + 1]
		for lx = 0, lw - 1 do
			if row:sub(lx + 1, lx + 1) == "#" then c:rect(x + lx - 1, y + ly - 1, 3, 3, outline) end
		end
	end
	for ly = 0, lh - 1 do
		local row = LOGO_ART[ly + 1]
		for lx = 0, lw - 1 do
			if row:sub(lx + 1, lx + 1) == "#" then c:dot(x + lx + 1, y + ly + 2, shadow) end
		end
	end
	for ly = 0, lh - 1 do
		local row = LOGO_ART[ly + 1]
		local fill = color_idx(cmix(top, bottom, 1.0 * ly / (lh - 1)))
		for lx = 0, lw - 1 do
			if row:sub(lx + 1, lx + 1) == "#" then c:dot(x + lx, y + ly, fill) end
		end
	end
	if not bleeding then return end
	local blood = rgb_idx(150, 14, 12)
	local drop = rgb_idx(210, 28, 22)
	local last = LOGO_ART[lh]
	for lx = 0, lw - 1 do
		if last:sub(lx + 1, lx + 1) == "#" and ember(lx, 21) % 4 == 0 then
			local length = 1 + ember(lx, 22) % 4
			c:rect(x + lx, y + lh, 1, length, blood)
			local fall = (G.tick + ember(lx, 23) % 60) % 60
			if fall < 24 then c:dot(x + lx, y + lh + length + fall // 2, drop) end
		end
	end
end

-- "key word   key word ..." centered, keys bright, words dim
local function controls_line(c, row, pairs_, key_color, word_color)
	local width = 0
	for i = 1, #pairs_, 2 do width = width + #pairs_[i] + 1 + #pairs_[i + 1] + 4 end
	local x = idivt(c.w - (width - 4), 2)
	for i = 1, #pairs_, 2 do
		c:text(x, row, pairs_[i], key_color)
		x = x + #pairs_[i] + 1
		c:text(x, row, pairs_[i + 1], word_color)
		x = x + #pairs_[i + 1] + 4
	end
end

function Screens:title_page()
	local g = G
	local c = self.page
	c:clear(PAL_BLACK)
	local height = c.h * 2
	local glow_at = height * 3 // 5
	c:gradient(0, glow_at, rgb(6, 5, 9), rgb(22, 8, 12))
	c:gradient(glow_at, height, rgb(22, 8, 12), rgb(84, 26, 12))
	self:draw_fire(c, height * 2 // 5)
	draw_embers(c, c.w // 2, rgb(255, 214, 120), rgb(110, 30, 16))

	local flicker = ember(g.tick // 3, 5) % 9 == 0
	local bone = flicker and rgb(190, 180, 160) or rgb(248, 238, 216)
	local logo_y = imax(2, height // 2 - 24)
	draw_logo(c, idivt(c.w - #LOGO_ART[1], 2), logo_y, bone, rgb(160, 24, 20), true)
	local row = (logo_y + #LOGO_ART) // 2 + 4
	c:text_centered(row, "a terminal raycaster, written in JaiScript", rgb_idx(186, 154, 130))
	if blink(12) then c:text_centered(row + 3, "-  press FIRE to descend  -", rgb_idx(255, 226, 160)) end
	local keys = rgb_idx(250, 214, 150)
	local words = rgb_idx(150, 132, 126)
	controls_line(c, row + 6, {"W S", "move", "A D", "strafe", "LEFT RIGHT", "turn", "SHIFT", "run"}, keys, words)
	controls_line(c, row + 7, {"SPACE", "fire", "E", "use", "1 2 3", "weapons", "M", "map", "ESC", "quit"}, keys, words)
	return c:rows(0, c.h)
end

local function clock_text(ticks)
	local secs = ticks // 30
	local pad = secs % 60 < 10 and "0" or ""
	return (secs // 60) .. ":" .. pad .. (secs % 60)
end

local function tally_row(c, x, row, name, count, percent, label, value)
	c:text(x, row, name, label)
	c:text(x + 10, row, count, value)
	c:text_right(x + 24, row, percent .. "%", value)
	c:rect(x + 26, row * 2, 10, 2, rgb_idx(40, 34, 40))
	c:rect(x + 26, row * 2, percent // 10, 2, percent == 100 and rgb_idx(120, 230, 110) or rgb_idx(226, 176, 70))
end

-- the intermission: the floor's numbers count up, DOOM style
function Screens:tally_page()
	local g = G
	local c = self.page
	c:clear(PAL_BLACK)
	local mood = MOODS[g.map_i + 1]
	local height = c.h * 2
	c:gradient(0, height, cmix(mood.fog, rgb(0, 0, 0), 0.3), cmix(mood.ceiling, mood.light, 0.12))
	local gold = shades5(rgb(255, 246, 200), rgb(220, 150, 60))
	local top = imax(2, height // 2 - 22)
	c:text_centered(top // 2, g.map_name, color_idx(mood.light))
	local head_h = heading(c, "FLOOR CLEARED", top + 4, 2, gold, rgb_idx(8, 6, 8))
	local row = (top + 4 + head_h) // 2 + 3
	local counted = g.mode_t * 2
	local kills = imin(g.kills, counted // 3)
	local secrets = imin(g.secrets, counted // 9)
	local time = imin(g.map_ticks, counted * 40)
	local kp = g.kill_total > 0 and kills * 100 // g.kill_total or 100
	local sp = g.secret_total > 0 and secrets * 100 // g.secret_total or 100
	local label = rgb_idx(170, 160, 150)
	local value = rgb_idx(255, 236, 190)
	local x = c.w // 2 - 17
	tally_row(c, x, row, "KILLS", kills .. " / " .. g.kill_total, kp, label, value)
	tally_row(c, x, row + 2, "SECRETS", secrets .. " / " .. g.secret_total, sp, label, value)
	c:text(x, row + 4, "TIME", label)
	c:text(x + 10, row + 4, clock_text(time), value)
	c:text(x + 18, row + 4, "PAR " .. clock_text(g.par), label)
	local done = kills == g.kills and secrets == g.secrets and time == g.map_ticks
	if done and blink(12) then
		local nxt = g.map_i + 1 < #MAPS and "press FIRE for the next floor" or "press FIRE to face the dark"
		c:text_centered(row + 8, nxt, rgb_idx(255, 226, 160))
	end
	return c:rows(0, c.h)
end

function Screens:dead_page()
	local g = G
	local c = self.page
	c:clear(PAL_BLACK)
	local height = c.h * 2
	c:gradient(0, height, rgb(36, 4, 6), rgb(8, 2, 3))
	-- blood runs down from the top, each run at its own pace and to its own depth
	local trail = rgb_idx(96, 8, 8)
	local bead = rgb_idx(214, 34, 28)
	for x = 0, c.w - 1 do
		if ember(x, 10) % 3 == 0 then
			local depth = height // 4 + ember(x, 13) % (height // 2)
			local length = imin(depth, g.mode_t * (2 + ember(x, 11) % 5) // 6 - ember(x, 12) % 12)
			for y = 0, length - 1 do c:dot(x, y, y == length - 1 and bead or trail) end
		end
	end
	local red = shades5(rgb(255, 120, 100), rgb(150, 10, 10))
	local top = height // 2 - 14
	local head_h = heading(c, "YOU DIED", top, 2, red, rgb_idx(6, 0, 0))
	local row = (top + head_h) // 2 + 2
	c:text_centered(row, "slain by " .. g.death_cause .. " on " .. g.map_name, rgb_idx(236, 200, 190))
	c:text_centered(row + 2, "kills " .. g.kills .. "/" .. g.kill_total .. "    deaths " .. g.deaths, rgb_idx(170, 130, 126))
	if g.mode_t > 25 and blink(12) then c:text_centered(row + 5, "press FIRE to rise again", rgb_idx(255, 226, 160)) end
	return c:rows(0, c.h)
end

function Screens:victory_page()
	local g = G
	local c = self.page
	c:clear(PAL_BLACK)
	local height = c.h * 2
	c:gradient(0, height, rgb(10, 8, 14), rgb(70, 52, 20))
	draw_embers(c, c.w // 3, rgb(255, 240, 170), rgb(120, 90, 30))
	local top = imax(2, height // 2 - 24)
	draw_logo(c, idivt(c.w - #LOGO_ART[1], 2), top, rgb(255, 250, 220), rgb(214, 140, 40), false)
	local row = (top + #LOGO_ART) // 2 + 4
	c:text_centered(row, "T H E   W A R D E N   I S   D O W N", rgb_idx(255, 230, 160))
	local label = rgb_idx(186, 170, 150)
	local x = c.w // 2 - 14
	c:text(x, row + 3, "EPISODE KILLS", label)
	c:text(x + 18, row + 3, g.ep_kills .. " / " .. g.ep_kill_total, rgb_idx(255, 246, 210))
	c:text(x, row + 4, "SECRETS", label)
	c:text(x + 18, row + 4, g.ep_secrets .. " / " .. g.ep_secret_total, rgb_idx(255, 246, 210))
	c:text(x, row + 5, "TIME", label)
	c:text(x + 18, row + 5, clock_text(g.ep_ticks), rgb_idx(255, 246, 210))
	c:text(x, row + 6, "DEATHS", label)
	c:text(x + 18, row + 6, tostring(g.deaths), rgb_idx(255, 246, 210))
	c:text_centered(row + 9, "the gloom lifts. for now.      ESC quits", rgb_idx(170, 150, 120))
	return c:rows(0, c.h)
end
