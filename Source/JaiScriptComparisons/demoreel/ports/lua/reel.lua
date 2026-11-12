-- The Scene base and the part of the Reel the perf path runs (Lua 5.4 port of scenes/reel.jai):
-- start a scene mid-flight, replay it, render it (scaled up past its pixel budget), the hit
-- iris and shake, present. The port starts mid-scene and runs no transitions, title cards or HUD.
--
-- A scene file (julia.lua is the model) ports one scenes/*.jai whole and registers its class
-- last: SCENES[2] = Julia (JULIA 2, GLOBHUNTER 6, OSSUARY 9, MAELSTROM 10). The Reel composes
-- cues and probes length_bars/hit_times/hit_levels only for registered scenes; make_scene of
-- any other index raises "scene I is not ported". Conventions:
-- * Arrays are 1-based tables holding the original's 0-based values: `grid[i]` is grid[i + 1],
--   a palette index p stays p and reads HB_FG[p + 1]. Lane:last returns a 1-based index or 0.
-- * A class: Name = setmetatable({}, {__index = Scene}); Name.__index = Name; Name.new() runs
--   Scene.init(setmetatable({}, Name)), then the field defaults, then the constructor body
--   (set pixel_budget there; replay is set wherever the original sets it, usually build).
--   Overrides are methods: Name:build(), :update(t, dt), :render(grid), :hit_times() ...
-- * The scene reaches self.pal (Palette: add/ramp/gradient return 0-based indices), self.rng
--   (the host's Rng: next/roll/chance/nextf), self.cue (Cue, nil outside the reel), self.w/h.
--   gfx (Gfx; gfx:make_grid(n)), RESET, MUSIC_*, HB_*/TXT_* and the text layer (txt_glyph,
--   txt_put, cell_col, ...) are globals; the reel rebinds the HB_ tables, so read them per call.
-- * Scope: what a file shares is global (classes, util functions, the tables above); a scene's
--   own state and functions (JU_*, julia_row) are file locals, upvalues being Lua's fast path;
--   hot code aliases globals and math functions into locals. A chunk holds at most 200 locals.
-- * Ints: itrunc/ifloor convert floats; idiv/imod are C's / and % where an operand can be
--   negative (Lua's // and % floor); math.floor returns an integer; a float must never reach
--   a string. parallel_transform(items, fn) runs fn over items serially, in order.

local sqrt, sin, cos = math.sqrt, math.sin, math.cos

SCENES = {}

Scene = {}
Scene.__index = Scene

function Scene.init(self)
	self.name = "SCENE"
	self.handle = ""
	self.w = 0
	self.h = 0
	self.seed = 0
	self.pal = nil
	self.rng = nil
	self.replay = 3.0          -- the sim-seconds a jump replays to arrive mid-flight
	self.cue = nil             -- the scene's music (music.lua): ask it for the kick, the chord, the energy, the hits
	self.pixel_budget = 0      -- set in the scene's constructor: past this many pixels the scene renders smaller and scales up
	self.titled = true         -- whether the reel lays its title card over the scene's first bar
	return self
end

function Scene:setup(w_, h_, seed_)
	self.w = w_
	self.h = h_
	self.seed = seed_
	self.rng = Rng.new(seed_)
	self.pal = Palette.new()
	self:build()
end

function Scene:build() end
function Scene:update(t, dt) end
function Scene:render(grid) end
function Scene:text_rows(rows) end
-- the scene's big moments in its own seconds (on beats or bar lines), and how big:
-- 2 an impact with a riser into it, 1 an accent; the composer scores them
function Scene:hit_times() return {} end
function Scene:hit_levels() return {} end
-- how many bars the scene needs for its showpiece to land in full; 0 takes the reel's default
function Scene:length_bars() return 0 end

Reel = {}
Reel.__index = Reel

function Reel.new(w_, h_, seed_, backend_, smoke_, hud_)
	local self = setmetatable({}, Reel)
	self.nscenes = 12
	self.intro_idx = 12        -- the cold open: one more entry past the scenes
	self.cues = {}             -- by scene index + 1, for the ported scenes only
	self.cur = nil
	self.cur_idx = 0
	self.scene_t = 0.0
	self.dur_cur = 10.0
	self.title_len = MUSIC_BAR
	self.in_trans = false
	self.finish_pending = false
	self.trans_t = 0.0
	self.paused = false
	self.flash_noise = {}      -- a threshold per pixel for the dithered flashes and the vignette
	self.ring = {}             -- per pixel: its distance from the centre, 0 there to 1000 at the corners
	self.small_a = {}          -- the scaled-down frame of a scene past its pixel budget
	self.ui_off = 0
	self.off_b = 0

	self.w = w_
	self.h = h_
	self.sext = PIX_MODE == "sext" or (PIX_MODE == "" and hb_pixel_w(w_, true) * hb_pixel_h(h_, true) <= PIX_BUDGET)
	self.pw = hb_pixel_w(w_, self.sext)
	self.ph = hb_pixel_h(h_, self.sext)
	self.seed = seed_
	self.backend = backend_
	self.smoke = smoke_
	self.hud = hud_
	self.grid_a = gfx:make_grid(self.pw * self.ph)
	self.grid_b = gfx:make_grid(self.pw * self.ph)
	self.melt = gfx:make_fgrid(self.pw)
	local ui = Palette.new()
	self.ui = ui
	self.ui_band = ui:size()   -- 12 band shades
	for b = 0, 11 do
		local f = b < 6 and b / 5.0 or (11 - b) / 5.0
		ui:add(10.0 + 14.0 * f, 8.0 + 8.0 * f, 30.0 + 26.0 * f)
	end
	self.ui_rule = ui:add(255, 206, 110)   -- 2 rule lines
	ui:add(110, 200, 255)
	self.ui_ink = ui:gradient(8, {{255.0, 250.0, 214.0}, {255.0, 214.0, 110.0}, {255.0, 128.0, 60.0}, {214.0, 52.0, 92.0}})
	self.ui_shine = ui:add(255, 255, 255)
	self.ui_shadow = ui:add(4, 2, 12)
	self.ui_edge = ui:add(255, 244, 200)
	self.bars = {32, 6, 6, 6, 7, 7, 8, 8, 6, 21, 8, 24, 4}               -- each scene's length in bars, at [idx + 1]
	self.seed_slots = {16, 3, 4, 5, 6, 8, 9, 10, 13, 15, 17, 23, 21}     -- each scene seeds from its own slot
	for i = 0, self.intro_idx do
		if SCENES[i] then
			local probe = self:make_scene(i)
			if probe:length_bars() > 0 then self.bars[i + 1] = probe:length_bars() end
			self.cues[i + 1] = Cue.new(self.bars[i + 1], music_mood(i), self.seed + self.seed_slots[i + 1] * 101, probe:hit_times(), probe:hit_levels())
		end
	end
	return self
end

function Reel:make_scene(idx)
	local class = SCENES[idx]
	if not class then error("scene " .. idx .. " is not ported", 0) end
	return class.new()
end

function Reel:dur_for(idx)
	return self.smoke and 3.0 or self.bars[idx + 1] * MUSIC_BAR
end

-- a scene at its grid, or at the largest grid within its pixel budget
function Reel:setup_scene(sc, idx)
	sc.cue = self.cues[idx + 1]
	local pw, ph = self.pw, self.ph
	local sw = pw
	local sh = ph
	if sc.pixel_budget > 0 and pw * ph > sc.pixel_budget then
		local k = sqrt(sc.pixel_budget * 1.0 / (pw * ph))
		sw = itrunc(pw * k)
		sh = itrunc(ph * k)
	end
	sc:setup(sw, sh, self.seed + self.seed_slots[idx + 1] * 7919)
end

-- render into a grid of the reel's size, scaling up a scene that renders smaller; returns the scratch grid
function Reel:render_scene(sc, target, scratch)
	local pw, ph = self.pw, self.ph
	if sc.w == pw and sc.h == ph then
		sc:render(target)
		return scratch
	end
	if #scratch ~= sc.w * sc.h then
		scratch = {}
		for i = 1, sc.w * sc.h do scratch[i] = 0 end
	end
	sc:render(scratch)
	local sw = sc.w
	local sh = sc.h
	local idx = 1
	for y = 0, ph - 1 do
		local row = (y * sh // ph) * sw + 1
		for x = 0, pw - 1 do
			target[idx] = scratch[row + x * sw // pw]
			idx = idx + 1
		end
	end
	return scratch
end

-- the presenter's string tables: the current scene's, then the reel's own UI entries
function Reel:bind_palettes()
	hb_ensure(self.w, self.h, self.sext)
	HB_FG = {}
	HB_BG = {}
	HB_RGB = {}
	HB_LUM = {}
	self:absorb(self.cur.pal)
	self.off_b = #HB_RGB
	self.ui_off = #HB_RGB
	self:absorb(self.ui)
end

function Reel:absorb(p)
	local fg, bg, rgb, lum = HB_FG, HB_BG, HB_RGB, HB_LUM
	local n = #p.rgb
	for i = 1, n do
		fg[#fg + 1] = p.fg[i]
		bg[#bg + 1] = p.bg[i]
		local c = p.rgb[i]
		rgb[#rgb + 1] = c
		lum[#lum + 1] = (c >> 16) * 2 + ((c >> 8) & 255) * 3 + (c & 255)
	end
end

function Reel:start_scene(idx, at_t)
	local nscenes = self.nscenes
	self.cur_idx = idx == self.intro_idx and idx or ((idx % nscenes) + nscenes) % nscenes
	self.cur = self:make_scene(self.cur_idx)
	self:setup_scene(self.cur, self.cur_idx)
	self.dur_cur = self:dur_for(self.cur_idx)
	self.scene_t = at_t
	self.in_trans = false
	self.finish_pending = false
	self.trans_t = 0.0
	self:bind_palettes()
end

-- a jump to a scene mid-flight: same scene, same clock; stateful scenes
-- replay up to their replay seconds so they arrive mid-flight instead of empty
function Reel:jump(idx, at_t)
	self:start_scene(idx, at_t)
	local cur = self.cur
	local steps = itrunc(at_t * 30.0)
	local most = itrunc(cur.replay * 30.0)
	steps = steps > most and most or steps
	local ft = at_t - steps * 0.0333333333
	for i = 0, steps - 1 do
		ft = ft + 0.0333333333
		cur:update(ft, 0.0333333333)
	end
end

function Reel:reload_current()
	self:start_scene(self.cur_idx, self.scene_t)
end

function Reel:ensure_noise()
	local pw, ph = self.pw, self.ph
	local n = pw * ph
	if #self.flash_noise == n then return end
	local noise_rng = Rng.new(self.seed + 977)
	local flash_noise = {}
	local ring = {}
	for i = 0, n - 1 do
		flash_noise[i + 1] = noise_rng:next(1000)
		local dx = (i % pw - pw * 0.5) / (pw * 0.5)
		local dy = (i // pw - ph * 0.5) / (ph * 0.5)
		ring[i + 1] = itrunc((dx * dx + dy * dy) * 500.0)
	end
	self.flash_noise = flash_noise
	self.ring = ring
end

function Reel:dither_flash(keep)
	self:ensure_noise()
	local pix = HB_PIX
	local shine = self.ui_off + self.ui_shine
	local noise = self.flash_noise
	for i = 1, self.pw * self.ph do
		if noise[i] < keep then pix[i] = shine end
	end
end

-- every big hit lands on the whole frame: over the bar before it an iris closes in,
-- tightest through the score's beat of silence, then the hit throws it open and
-- shakes the picture
function Reel:drop_fx(sc, t)
	local cue = sc.cue
	if cue == nil then return end
	local hit_t, hit_level = cue.hit_t, cue.hit_level
	local close = 0.0
	local since = 9.0
	for k = 1, #hit_t do
		if hit_level[k] >= 2 then
			local to = hit_t[k] - t
			if to > 0.0 and to <= MUSIC_BAR then
				local c = 1.0 - to / MUSIC_BAR
				c = to <= MUSIC_STEP * 4.0 and 0.62 or c * c * 0.45
				close = c > close and c or close
			end
			if to <= 0.0 and 0.0 - to < since then since = 0.0 - to end
		end
	end
	if close > 0.0 then
		self:ensure_noise()
		local pix = HB_PIX
		local noise = self.flash_noise
		local rg = self.ring
		local dark = self.ui_off + self.ui_shadow
		-- past the iris everything is dark, across a thin rim it dithers in
		local edge = itrunc((1.04 - close) * 1000.0)
		local rim = 70
		for i = 1, self.pw * self.ph do
			local over = rg[i] - edge + rim
			if over > 0 and (over >= rim or noise[i] * rim < over * 1000) then pix[i] = dark end
		end
	end
	if since < 0.32 then
		local amp = (0.32 - since) / 0.32 * (self.sext and 4.0 or 2.5)
		local dx = itrunc(sin(t * 97.0) * amp)
		local dy = itrunc(cos(t * 71.0) * amp * 0.6)
		if dx ~= 0 or dy ~= 0 then self:shift_frame(dx, dy) end
	end
end

-- the frame moved by (dx, dy) pixels, its edges smeared in
function Reel:shift_frame(dx, dy)
	local pw, ph = self.pw, self.ph
	local pix = HB_PIX
	local tmp = self.grid_b
	local n = pw * ph
	for i = 1, n do tmp[i] = pix[i] end
	local i2 = 1
	for y = 0, ph - 1 do
		local sy = y - dy
		sy = sy < 0 and 0 or (sy >= ph and ph - 1 or sy)
		local row = sy * pw + 1
		for x = 0, pw - 1 do
			local sx = x - dx
			sx = sx < 0 and 0 or (sx >= pw and pw - 1 or sx)
			pix[i2] = tmp[row + sx]
			i2 = i2 + 1
		end
	end
end

function Reel:frame(t, dt, key, fps, ms_sim, ms_draw, ms_other)
	local cur = self.cur
	if cur == nil then error("the port starts mid-scene: jump to a scene first", 0) end
	hb_ensure(self.w, self.h, self.sext)
	txt_clear()
	if #HB_RGB == 0 then self:bind_palettes() end
	local step = self.paused and 0.0 or dt
	self.scene_t = self.scene_t + step
	if self.scene_t >= self.dur_cur then error("the scene ran out: the port starts mid-scene and runs no transitions", 0) end
	cur:update(self.scene_t, step)
	self.small_a = self:render_scene(cur, HB_PIX, self.small_a)
	self:drop_fx(cur, self.scene_t)
	if self.scene_t < self.title_len and cur.titled then error("a title card is up: the port starts mid-scene and draws none", 0) end
	local rows = hb_present()
	cur:text_rows(rows)
	return table.concat(rows, "\n")
end
